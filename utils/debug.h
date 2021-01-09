#include <iostream>

extern bool debug_output;

#ifdef _DEBUG
#define DEBUG_PRINT(...) \
    if(debug_output){ std::cout << __VA_ARGS__; }
#else
#define DEBUG_PRINT(...)
#endif