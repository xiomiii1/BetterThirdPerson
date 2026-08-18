#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace betterthirdperson::memory {
enum class SignatureId : std::uint16_t {
    NormalTick,
    GetPerspective,
    LocalPlayerApplyTurnDelta,
    ClientInstanceGetLocalPlayer,
    Count
};
inline constexpr std::size_t SignatureCount = static_cast<std::size_t>(SignatureId::Count);
struct SignatureDefinition { SignatureId id; std::string_view pattern; };
bool resolveAll(std::string_view libraryName = "libminecraftpe.so");
std::uintptr_t resolve(SignatureId id);
void clear();
}
