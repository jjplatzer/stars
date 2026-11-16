file(READ "${INPUT}" data HEX)
string(REGEX REPLACE "(..)" "0x\\1," data "${data}")
file(WRITE "${OUTPUT}" "#pragma once\n#include <cstddef>\nalignas(4) inline constexpr unsigned char ${NAME}[] = {${data}0};\ninline constexpr std::size_t ${NAME}_size = sizeof(${NAME}) - 1;\n")
