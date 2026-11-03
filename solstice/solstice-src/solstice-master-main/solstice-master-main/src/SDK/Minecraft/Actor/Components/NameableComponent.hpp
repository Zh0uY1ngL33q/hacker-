#pragma once

#include <string>

/// <summary>
/// The component that owns an actor's name tag.
///
/// Declared with the `class` keyword on purpose: entt hashes the type name
/// including the keyword MSVC prints, and ComponentHashes.hpp lists this one as
/// "class NameableComponent" (0xE2748E81) while every other component there is
/// listed as "struct". Declaring it as a struct would hash to a different value
/// and the lookup would silently miss.
///
/// The layout is NOT verified for 1.21.120 -- only the type name is known, from
/// the hash table. Actor::getNameTag therefore validates whatever it reads here
/// before using it, and prints the bytes once when they do not read as a string.
/// </summary>
class NameableComponent
{
public:
    std::string mName;
};
