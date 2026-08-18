#include "Hooks.hpp"
#include <betterthirdperson/Metadata.hpp>
#include <betterthirdperson/Signatures.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>
#include <entt/entt.hpp>
#include <pl/Mod.hpp>

namespace {
using betterthirdperson::memory::SignatureId;

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

template <std::size_t N, typename T>
struct Bitset {
    T value{};
    bool test(std::size_t index) const { return (value & (static_cast<T>(1) << index)) != 0; }
};

struct MoveInputState {
    Bitset<27, std::uint32_t> mFlagValues;
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
    Bitset<11, std::uint16_t> mFlagValues;
    std::array<bool, 2> mIsPaddling;
};

enum class EntityId : std::uint32_t {};
struct EntityIdTraits {
    using value_type = EntityId; using entity_type = std::uint32_t;
    using version_type = std::uint16_t;
    static constexpr std::uint32_t entity_mask = 0x3FFFF;
    static constexpr std::uint32_t version_mask = 0x3FFF;
};
}
namespace entt {
template<> struct entt_traits<EntityId> : entt::basic_entt_traits<EntityIdTraits> {
    static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
};
}
namespace {
class EntityContext {
public:
    template<class T> T* tryGet() { return mEnTTRegistry.try_get<T>(mEntity); }
    entt::basic_registry<EntityId>& mRegistry;
    entt::basic_registry<EntityId>& mEnTTRegistry;
    EntityId const mEntity;
};

static std::atomic<void*> g_localPlayer{nullptr};
static std::atomic<int> g_perspective{0};
static std::atomic<bool> g_enabled{true};
static std::mutex g_stateMutex;
static float g_cameraYaw = 0.0f;
static float g_cameraPitch = 0.0f;
static float g_bodyYaw = 0.0f;
static bool g_haveCamera = false;
static bool g_hooksInstalled = false;

static bool (*g_getPerspectiveOriginal)(void*) = nullptr;
static void (*g_turnOriginal)(void*, Vec2*) = nullptr;
static void (*g_tickOriginal)(void*) = nullptr;
static void* (*g_getLocalPlayerOriginal)(void*) = nullptr;

static betterthirdperson::hooks::Handle hPerspective = nullptr;
static betterthirdperson::hooks::Handle hTurn = nullptr;
static betterthirdperson::hooks::Handle hTick = nullptr;
static betterthirdperson::hooks::Handle hLocalPlayer = nullptr;

constexpr std::size_t ACTOR_ENTITY_CONTEXT = 0x8;
constexpr std::size_t ACTOR_ROTATION_COMPONENT = 0x218;

static void* getEntityContext(void* player) {
    if (!player) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + ACTOR_ENTITY_CONTEXT);
}

static void readBodyRotation(void* player) {
    if (!player) return;
    auto* component = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + ACTOR_ROTATION_COMPONENT);
    if (!component) return;
    const auto* rotation = reinterpret_cast<const Vec2*>(component);
    g_bodyYaw = rotation->y;
}

static MoveInputComponent* moveInput(void* player) {
    auto* context = reinterpret_cast<EntityContext*>(getEntityContext(player));
    if (!context) return nullptr;
    return context->tryGet<MoveInputComponent>();
}

static bool isThirdPerson() {
    const int p = g_perspective.load(std::memory_order_relaxed);
    return p == 1 || p == 2;
}

static float wrapDegrees(float value) {
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

static void initializeCameraFromPlayer(void* player) {
    if (g_haveCamera || !player) return;
    auto* component = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + ACTOR_ROTATION_COMPONENT);
    if (!component) return;
    const auto rotation = *reinterpret_cast<const Vec2*>(component);
    g_cameraPitch = rotation.x;
    g_cameraYaw = rotation.y;
    g_bodyYaw = rotation.y;
    g_haveCamera = true;
}

static void updateCameraInput(void* player) {
    if (!g_enabled.load(std::memory_order_relaxed) || !player || !isThirdPerson()) return;
    auto* input = moveInput(player);
    if (!input) return;
    std::lock_guard lock(g_stateMutex);
    initializeCameraFromPlayer(player);
    input->mCameraOrientation = {g_cameraPitch, g_cameraYaw, 0.0f};

    // Re-express local movement in camera space without changing the body's yaw.
    const float forward = input->mRawInputState.mAnalogMoveVector.y;
    const float strafe = input->mRawInputState.mAnalogMoveVector.x;
    if (std::abs(forward) < 0.0001f && std::abs(strafe) < 0.0001f) return;

    const float delta = (g_cameraYaw - g_bodyYaw) * 3.14159265358979323846f / 180.0f;
    const float c = std::cos(delta);
    const float s = std::sin(delta);
    const float localX = strafe * c - forward * s;
    const float localY = strafe * s + forward * c;
    input->mMove = {localX, localY};
}

static int getPerspectiveDetour(void* self) {
    const int result = g_getPerspectiveOriginal ? g_getPerspectiveOriginal(self) : 0;
    const int old = g_perspective.exchange(result, std::memory_order_relaxed);
    const bool wasThird = old == 1 || old == 2;
    const bool isThird = result == 1 || result == 2;
    if (!wasThird && isThird) {
        std::lock_guard lock(g_stateMutex);
        g_haveCamera = false;
    }
    return result;
}

static void turnDetour(void* self, Vec2* delta) {
    if (self && !g_localPlayer.load(std::memory_order_acquire)) g_localPlayer.store(self, std::memory_order_release);
    if (!delta || !g_enabled.load(std::memory_order_relaxed) || !isThirdPerson()) {
        if (g_turnOriginal) g_turnOriginal(self, delta);
        return;
    }

    {
        std::lock_guard lock(g_stateMutex);
        initializeCameraFromPlayer(self);
        g_cameraPitch = std::clamp(g_cameraPitch + delta->x, -89.9f, 89.9f);
        g_cameraYaw = wrapDegrees(g_cameraYaw + delta->y);
    }

    // Keep actor/body rotation unchanged. Camera orientation is written to MoveInputComponent.
    Vec2 zero{0.0f, 0.0f};
    if (g_turnOriginal) g_turnOriginal(self, &zero);
}

static void* getLocalPlayerDetour(void* self) {
    void* player = g_getLocalPlayerOriginal ? g_getLocalPlayerOriginal(self) : nullptr;
    if (player) g_localPlayer.store(player, std::memory_order_release);
    return player;
}

static void tickDetour(void* actor) {
    if (actor == g_localPlayer.load(std::memory_order_acquire)) {
        readBodyRotation(actor);
        updateCameraInput(actor);
    }
    if (g_tickOriginal) g_tickOriginal(actor);
}

static bool installOne(betterthirdperson::memory::SignatureId id, void* detour, void** original, betterthirdperson::hooks::Handle& handle) {
    const auto address = betterthirdperson::memory::resolve(id);
    if (!address) return false;
    handle = betterthirdperson::hooks::install(reinterpret_cast<void*>(address), detour, original);
    return handle != nullptr;
}

class BetterThirdPersonMod {
public:
    bool load(pl::mod::ModContext&) {
        if (g_hooksInstalled) return true;
        if (!betterthirdperson::memory::resolveAll()) return false;
        const bool p = installOne(SignatureId::GetPerspective, reinterpret_cast<void*>(getPerspectiveDetour), reinterpret_cast<void**>(&g_getPerspectiveOriginal), hPerspective);
        const bool t = installOne(SignatureId::LocalPlayerApplyTurnDelta, reinterpret_cast<void*>(turnDetour), reinterpret_cast<void**>(&g_turnOriginal), hTurn);
        const bool n = installOne(SignatureId::NormalTick, reinterpret_cast<void*>(tickDetour), reinterpret_cast<void**>(&g_tickOriginal), hTick);
        const bool l = installOne(SignatureId::ClientInstanceGetLocalPlayer, reinterpret_cast<void*>(getLocalPlayerDetour), reinterpret_cast<void**>(&g_getLocalPlayerOriginal), hLocalPlayer);
        g_hooksInstalled = p && t && n && l;
        return g_hooksInstalled;
    }
    bool enable(pl::mod::ModContext&) { g_enabled.store(true); return true; }
    bool disable(pl::mod::ModContext&) { g_enabled.store(false); return true; }
    bool unload(pl::mod::ModContext&) {
        g_enabled.store(false);
        if (hLocalPlayer) { betterthirdperson::hooks::remove(hLocalPlayer); hLocalPlayer = nullptr; }
        if (hTick) { betterthirdperson::hooks::remove(hTick); hTick = nullptr; }
        if (hTurn) { betterthirdperson::hooks::remove(hTurn); hTurn = nullptr; }
        if (hPerspective) { betterthirdperson::hooks::remove(hPerspective); hPerspective = nullptr; }
        betterthirdperson::memory::clear();
        g_hooksInstalled = false;
        return true;
    }
};
}

static BetterThirdPersonMod g_mod;
PL_REGISTER_MOD(BetterThirdPersonMod, g_mod)
