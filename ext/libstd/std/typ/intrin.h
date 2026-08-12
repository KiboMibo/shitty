#pragma once

// portable defines for compiler intrinsics

#define stdIsTriviallyCopyable __is_trivially_copyable
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 16
    #define stdHasTrivialDestructor __has_trivial_destructor
#else
    #define stdHasTrivialDestructor __is_trivially_destructible
#endif
#define stdIsTrivial __is_trivial
#define stdIsStandardLayout __is_standard_layout
