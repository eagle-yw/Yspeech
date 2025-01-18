export module yspeech.Common.Types;

import std;
namespace yspeech {

export using Bytes = std::vector<std::uint8_t>;
export using BytesView = const std::vector<std::uint8_t>&;
export using Byte = std::uint8_t;
export using Size = std::size_t;



// concept, copyable
export template <typename T>
concept Copyable = std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>;
// concept, Not copyable
export template <typename T>
concept NonCopyable = !Copyable<T>;

// concept, moveable
export template <typename T>
concept Movable = std::is_move_constructible_v<T> && std::is_move_assignable_v<T>;
// concept, Not moveable
export template <typename T>
concept NonMovable = !Movable<T>;




}