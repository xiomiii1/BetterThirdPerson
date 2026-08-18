#include <betterthirdperson/Signatures.hpp>

#include <array>
#include <string>
#include <vector>

#include <pl/memory/Signature.hpp>

namespace betterthirdperson::memory {

namespace {

std::array<
    std::uintptr_t,
    SignatureCount
> addresses{};

const std::array<
    SignatureDefinition,
    SignatureCount
> definitions{{
    {
        SignatureId::NormalTick,

        "? ? ? FC "
        "? ? ? A9 "
        "? ? ? A9 "
        "? ? ? A9 "
        "? ? ? A9 "
        "? ? ? A9 "
        "? ? ? A9 "
        "? ? ? 91 "
        "? ? ? D1 "
        "54 D0 3B D5 "
        "F3 03 00 AA "
        "? ? ? F9 "
        "? ? ? F8 "
        "? ? ? 39"
    },

    {
        SignatureId::GetPerspective,

        "? ? ? A9 "
        "FD 03 00 91 "
        "? ? ? F9 "
        "? ? ? F9 "
        "? ? ? F9 "
        "00 01 3F D6 "
        "? ? ? F9 "
        "? ? ? F9 "
        "? ? ? A8 "
        "20 00 1F D6 "
        "? ? ? A9 "
        "FD 03 00 91"
    },

    {
        SignatureId::LocalPlayerApplyTurnDelta,

        "? ? ? D1 "
        "? ? ? FD "
        "? ? ? A9 "
        "? ? ? A9 "
        "? ? ? A9 "
        "? ? ? A9 "
        "? ? ? 91 "
        "56 D0 3B D5 "
        "F3 03 00 AA "
        "F4 03 01 AA "
        "? ? ? F9 "
        "? ? ? F8 "
        "? ? ? F9 "
        "? ? ? F9"
    },

    {
        SignatureId::ClientInstanceGetLocalPlayer,

        "? ? ? D1 "
        "? ? ? A9 "
        "? ? ? F9 "
        "? ? ? 91 "
        "53 D0 3B D5 "
        "E8 03 00 AA "
        "? ? ? 91 "
        "? ? ? F9 "
        "? ? ? 91 "
        "? ? ? F8 "
        "? ? ? 95 "
        "? ? ? 91 "
        "? ? ? 95 "
        "? ? ? 36 "
        "? ? ? 91 "
        "? ? ? 52 "
        "? ? ? 94"
    }
}};

}

bool resolveAll(
    std::string_view libraryName
) {
    const std::string library(
        libraryName
    );

    std::vector<std::string> patterns;

    patterns.reserve(
        definitions.size()
    );

    for (
        const auto& definition :
        definitions
    ) {
        patterns.emplace_back(
            definition.pattern
        );
    }

    const auto resolved =
        pl::memory::resolveSignatures(
            patterns,
            library.c_str()
        );

    addresses.fill(0);

    bool anyResolved = false;

    for (
        std::size_t i = 0;
        i < definitions.size();
        ++i
    ) {
        const auto iterator =
            resolved.find(
                patterns[i]
            );

        if (
            iterator == resolved.end() ||
            iterator->second == 0
        ) {
            continue;
        }

        addresses[
            static_cast<std::size_t>(
                definitions[i].id
            )
        ] = iterator->second;

        anyResolved = true;
    }

    return anyResolved;
}

std::uintptr_t resolve(
    SignatureId id
) {
    const auto index =
        static_cast<std::size_t>(
            id
        );

    return
        index < addresses.size()
            ? addresses[index]
            : 0;
}

void clear() {
    addresses.fill(0);
}

}
