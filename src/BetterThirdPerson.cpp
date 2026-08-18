#include "Hooks.hpp"

#include <betterthirdperson/Metadata.hpp>
#include <betterthirdperson/Signatures.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

#include <entt/entt.hpp>
#include <pl/Mod.hpp>

namespace {

using betterthirdperson::memory::SignatureId;

struct Vec2 {
    float x;
    float y;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

template <std::size_t N, typename T>
struct bitset {
    T value{};

    bool test(std::size_t index) const {
        if (index >= sizeof(T) * 8) {
            return false;
        }

        return (value & (static_cast<T>(1) << index)) != 0;
    }
};

struct MoveInputState {
    bitset<27, std::uint32_t> mFlagValues;
    Vec2 mAnalogMoveVector;
    std::uint8_t mLookSlightDirField;
    std::uint8_t mLookNormalDirField;
    std::uint8_t mLookSmoothDirField;
    std::uint8_t pad[1];
};

struct MoveInputComponent {
    MoveInputState mInputState;
    MoveInputState mRawInputState;

    std::uint8_t mHoldAutoJumpInWaterTicks;
    std::uint8_t pad[3];

    Vec2 mMove;
    Vec2 mLookDelta;
    Vec2 mInteractDir;

    Vec3 mDisplacement;
    Vec3 mDisplacementDelta;
    Vec3 mCameraOrientation;

    bitset<11, std::uint16_t> mFlagValues;

    std::array<bool, 2> mIsPaddling;
};

enum class EntityId : std::uint32_t {};

struct EntityIdTraits {
    using value_type = EntityId;
    using entity_type = std::uint32_t;
    using version_type = std::uint16_t;

    static constexpr std::uint32_t entity_mask = 0x3FFFF;
    static constexpr std::uint32_t version_mask = 0x3FFF;
};

}

namespace entt {

template <>
struct entt_traits<EntityId>
    : entt::basic_entt_traits<EntityIdTraits> {

    static constexpr std::size_t page_size =
        ENTT_SPARSE_PAGE;
};

}

namespace {

class EntityContext {
public:
    template <typename T>
    T* tryGetComponent() {
        return mEnTTRegistry.try_get<T>(mEntity);
    }

    entt::basic_registry<EntityId>& mRegistry;
    entt::basic_registry<EntityId>& mEnTTRegistry;
    EntityId const mEntity;
};

/*
 * These offsets are the offsets used by the supplied
 * BedrockTools source.
 *
 * They are isolated here so they can be updated for a
 * specific Bedrock build without changing the camera code.
 */
constexpr std::size_t kActorEntityContext = 0x8;
constexpr std::size_t kActorRotationComponent = 0x218;

/*
 * Runtime state.
 */

static std::atomic<void*> g_localPlayer{
    nullptr
};

static std::atomic<bool> g_enabled{
    true
};

static std::atomic<bool> g_inThirdPerson{
    false
};

static std::mutex g_stateMutex;

static float g_cameraYaw = 0.0f;
static float g_cameraPitch = 0.0f;
static float g_bodyYaw = 0.0f;

static bool g_haveCamera = false;
static bool g_hooksInstalled = false;

/*
 * Original functions.
 */

static void (*g_turnOriginal)(
    void*,
    Vec2*
) = nullptr;

static void (*g_tickOriginal)(
    void*
) = nullptr;

static int (*g_getPerspectiveOriginal)(
    void*
) = nullptr;

/*
 * Hook handles.
 */

static betterthirdperson::hooks::Handle
    hTurn = nullptr;

static betterthirdperson::hooks::Handle
    hTick = nullptr;

static betterthirdperson::hooks::Handle
    hPerspective = nullptr;

/*
 * Helpers.
 */

static float wrapDegrees(float value) {
    while (value > 180.0f) {
        value -= 360.0f;
    }

    while (value < -180.0f) {
        value += 360.0f;
    }

    return value;
}

static bool isThirdPersonResult(
    int perspective
) {
    /*
     * Bedrock perspective values:
     *
     * 0 = first person
     * 1 = back third person
     * 2 = front third person
     */

    return (
        perspective == 1 ||
        perspective == 2
    );
}

/*
 * Retrieve the MoveInputComponent from
 * the actor's entity context.
 */

static MoveInputComponent*
getMoveInputComponent(
    void* actor
) {
    if (!actor) {
        return nullptr;
    }

    const auto address =
        reinterpret_cast<
            std::uintptr_t
        >(actor);

    const auto contextAddress =
        *reinterpret_cast<
            std::uintptr_t*
        >(
            address +
            kActorEntityContext
        );

    if (contextAddress == 0) {
        return nullptr;
    }

    auto* context =
        reinterpret_cast<
            EntityContext*
        >(
            contextAddress
        );

    return context->
        tryGetComponent<
            MoveInputComponent
        >();
}

/*
 * Read the actor's current body rotation.
 */

static bool tryReadBodyRotation(
    void* actor,
    float& yaw
) {
    if (!actor) {
        return false;
    }

    const auto address =
        reinterpret_cast<
            std::uintptr_t
        >(actor);

    const auto componentAddress =
        *reinterpret_cast<
            std::uintptr_t*
        >(
            address +
            kActorRotationComponent
        );

    if (componentAddress == 0) {
        return false;
    }

    const auto* rotation =
        reinterpret_cast<
            const Vec2*
        >(
            componentAddress
        );

    if (
        !std::isfinite(rotation->x) ||
        !std::isfinite(rotation->y)
    ) {
        return false;
    }

    yaw = rotation->y;

    return true;
}

/*
 * Initialize the independent camera
 * from the player's current body rotation.
 */

static void initializeCameraFromBody(
    void* actor
) {
    if (
        g_haveCamera ||
        !actor
    ) {
        return;
    }

    float bodyYaw = 0.0f;

    if (
        !tryReadBodyRotation(
            actor,
            bodyYaw
        )
    ) {
        return;
    }

    g_bodyYaw =
        bodyYaw;

    g_cameraYaw =
        bodyYaw;

    g_cameraPitch =
        0.0f;

    g_haveCamera =
        true;
}

/*
 * Update Bedrock's camera orientation and
 * transform movement relative to the camera.
 */

static void updateCameraOrientation(
    void* actor
) {
    if (
        !actor ||
        !g_inThirdPerson.load(
            std::memory_order_relaxed
        )
    ) {
        return;
    }

    if (
        !g_enabled.load(
            std::memory_order_relaxed
        )
    ) {
        return;
    }

    auto* input =
        getMoveInputComponent(
            actor
        );

    if (!input) {
        return;
    }

    std::lock_guard<std::mutex> lock(
        g_stateMutex
    );

    initializeCameraFromBody(
        actor
    );

    if (!g_haveCamera) {
        return;
    }

    /*
     * Keep camera orientation independent
     * from body rotation.
     */
    input->mCameraOrientation = {
        g_cameraPitch,
        g_cameraYaw,
        0.0f
    };

    const float forward =
        input->
            mRawInputState
            .mAnalogMoveVector
            .y;

    const float strafe =
        input->
            mRawInputState
            .mAnalogMoveVector
            .x;

    if (
        std::abs(forward) < 0.0001f &&
        std::abs(strafe) < 0.0001f
    ) {
        return;
    }

    constexpr float kPi =
        3.14159265358979323846f;

    const float delta =
        (
            g_cameraYaw -
            g_bodyYaw
        ) *
        kPi /
        180.0f;

    const float c =
        std::cos(delta);

    const float s =
        std::sin(delta);

    input->mMove = {
        strafe * c -
            forward * s,

        strafe * s +
            forward * c
    };
}

/*
 * Perspective hook.
 */

static int getPerspectiveDetour(
    void* self
) {
    const int result =
        g_getPerspectiveOriginal
            ? g_getPerspectiveOriginal(
                self
            )
            : 0;

    const bool thirdPerson =
        isThirdPersonResult(
            result
        );

    const bool previous =
        g_inThirdPerson.exchange(
            thirdPerson,
            std::memory_order_acq_rel
        );

    if (
        thirdPerson != previous
    ) {
        std::lock_guard<std::mutex> lock(
            g_stateMutex
        );

        g_haveCamera =
            false;
    }

    return result;
}

/*
 * Camera rotation hook.
 */

static void turnDetour(
    void* self,
    Vec2* rotationDelta
) {
    if (
        !self ||
        !rotationDelta ||
        !std::isfinite(
            rotationDelta->x
        ) ||
        !std::isfinite(
            rotationDelta->y
        )
    ) {
        if (g_turnOriginal) {
            g_turnOriginal(
                self,
                rotationDelta
            );
        }

        return;
    }

    /*
     * The LocalPlayerApplyTurnDelta
     * hook gives us the player object,
     * so an additional local-player
     * hook isn't necessary.
     */
    g_localPlayer.store(
        self,
        std::memory_order_release
    );

    if (
        !g_enabled.load(
            std::memory_order_relaxed
        ) ||
        !g_inThirdPerson.load(
            std::memory_order_relaxed
        )
    ) {
        if (g_turnOriginal) {
            g_turnOriginal(
                self,
                rotationDelta
            );
        }

        return;
    }

    {
        std::lock_guard<std::mutex> lock(
            g_stateMutex
        );

        initializeCameraFromBody(
            self
        );

        if (!g_haveCamera) {
            if (g_turnOriginal) {
                g_turnOriginal(
                    self,
                    rotationDelta
                );
            }

            return;
        }

        g_cameraPitch =
            std::clamp(
                g_cameraPitch +
                    rotationDelta->x,
                -89.9f,
                89.9f
            );

        g_cameraYaw =
            wrapDegrees(
                g_cameraYaw +
                    rotationDelta->y
            );
    }

    /*
     * Don't apply the camera rotation
     * directly to the player's body.
     */
    Vec2 zero{
        0.0f,
        0.0f
    };

    if (g_turnOriginal) {
        g_turnOriginal(
            self,
            &zero
        );
    }

    /*
     * Update the native camera orientation
     * after the game's normal turn handling.
     */
    updateCameraOrientation(
        self
    );
}

/*
 * Tick hook.
 */

static void tickDetour(
    void* actor
) {
    /*
     * IMPORTANT:
     *
     * Let Bedrock complete its normal tick
     * before modifying MoveInputComponent.
     */
    if (g_tickOriginal) {
        g_tickOriginal(
            actor
        );
    }

    if (
        actor !=
            g_localPlayer.load(
                std::memory_order_acquire
            ) ||
        !g_enabled.load(
            std::memory_order_relaxed
        ) ||
        !g_inThirdPerson.load(
            std::memory_order_relaxed
        )
    ) {
        return;
    }

    float currentBodyYaw =
        0.0f;

    if (
        tryReadBodyRotation(
            actor,
            currentBodyYaw
        )
    ) {
        std::lock_guard<std::mutex> lock(
            g_stateMutex
        );

        g_bodyYaw =
            currentBodyYaw;
    }

    updateCameraOrientation(
        actor
    );
}

/*
 * Install a single hook.
 */

static bool installOne(
    SignatureId id,
    void* detour,
    void** original,
    betterthirdperson::hooks::Handle& handle
) {
    const auto address =
        betterthirdperson::memory::resolve(
            id
        );

    if (!address) {
        return false;
    }

    handle =
        betterthirdperson::hooks::install(
            reinterpret_cast<void*>(
                address
            ),
            detour,
            original
        );

    return handle != nullptr;
}

/*
 * Mod implementation.
 */

class BetterThirdPersonMod {

public:

    bool load(
        pl::mod::ModContext&
    ) {
        if (g_hooksInstalled) {
            return true;
        }

        if (
            !betterthirdperson::memory::
                resolveAll()
        ) {
            return false;
        }

        /*
         * Only install the hooks actually
         * required by this implementation.
         *
         * ClientInstanceGetLocalPlayer was
         * removed because turn input already
         * provides the local player object.
         */

        const bool perspective =
            installOne(
                SignatureId::GetPerspective,

                reinterpret_cast<void*>(
                    getPerspectiveDetour
                ),

                reinterpret_cast<void**>(
                    &g_getPerspectiveOriginal
                ),

                hPerspective
            );

        const bool turn =
            installOne(
                SignatureId::
                    LocalPlayerApplyTurnDelta,

                reinterpret_cast<void*>(
                    turnDetour
                ),

                reinterpret_cast<void**>(
                    &g_turnOriginal
                ),

                hTurn
            );

        const bool tick =
            installOne(
                SignatureId::NormalTick,

                reinterpret_cast<void*>(
                    tickDetour
                ),

                reinterpret_cast<void**>(
                    &g_tickOriginal
                ),

                hTick
            );

        g_hooksInstalled =
            perspective &&
            turn &&
            tick;

        if (!g_hooksInstalled) {
            unloadInternal();
            return false;
        }

        return true;
    }

    bool enable(
        pl::mod::ModContext&
    ) {
        g_enabled.store(
            true,
            std::memory_order_release
        );

        return true;
    }

    bool disable(
        pl::mod::ModContext&
    ) {
        g_enabled.store(
            false,
            std::memory_order_release
        );

        return true;
    }

    bool unload(
        pl::mod::ModContext&
    ) {
        return unloadInternal();
    }

private:

    bool unloadInternal() {

        g_enabled.store(
            false,
            std::memory_order_release
        );

        g_inThirdPerson.store(
            false,
            std::memory_order_release
        );

        /*
         * Remove hooks in reverse order.
         */

        if (hTick) {
            betterthirdperson::hooks::remove(
                hTick
            );

            hTick =
                nullptr;
        }

        if (hTurn) {
            betterthirdperson::hooks::remove(
                hTurn
            );

            hTurn =
                nullptr;
        }

        if (hPerspective) {
            betterthirdperson::hooks::remove(
                hPerspective
            );

            hPerspective =
                nullptr;
        }

        betterthirdperson::memory::clear();

        g_localPlayer.store(
            nullptr,
            std::memory_order_release
        );

        {
            std::lock_guard<std::mutex> lock(
                g_stateMutex
            );

            g_cameraYaw =
                0.0f;

            g_cameraPitch =
                0.0f;

            g_bodyYaw =
                0.0f;

            g_haveCamera =
                false;
        }

        g_hooksInstalled =
            false;

        return true;
    }
};

}

static BetterThirdPersonMod
    g_mod;

PL_REGISTER_MOD(
    BetterThirdPersonMod,
    g_mod
);
