# Coding Convention

In general, here is my preference for any languages:
- You are recommended to debug the compiled binary: 
  - Once it crashes.
  - When after one attemp of failed guessing to fix.
- I am a fan of crash early. When something should happen, it should just happen, do not play a game like "what if it is not the case" and silently covers the issue. One example is that, if an object should not be null, then we should just use it, if a nullable object should not be null, we should just cast it. No test is performed in this case, using it will crash if it is null, and we know there is a problem. Fix the actual problem instead of doing "error tolerance".
- I am a fan of **DO NOT REPEAT YOURSELF (DRY)**.
  - DRY focus on not repeating information in source code. For example, compiler always do name mangling, but name mangling is complex. If you implementaion the mangling in two different places, you repeat the information twice. Therefore a function for such thing is always needed.
  - DRY does not focus on not repeating some code. For example, create `json::JsonString` requires filling its field. The way to creat it does not offer any new information. So a three-lines function just to create `json::JsonString` and copy the argument to its field is not needed.
    - But if building an AST requires significantly more lines of code, extracting functions for the work is preferred.
  - DRY requires finding if a feature has already been implemented somewhere else before implementing it. avoiding massive duplication.
    - If the existing implementation is not sharable, refactoring is preferred.

## C++ Coding Convention

- Although C++ does not require this but we want to have `extern` on all function forward declarations.
  - In general we don't use `inline` in header files unless such function is performance critical, e.g. very simple comparison operators.
- Rules for C++ header files:
  - In a class/struct/union declaration, member names must be aligned in the same column at least in the same public, protected or private section.
  - Keep the coding style consistent with other header files in the same project.
- Extra Rules for C++ header files in `Source` folder:
  - Do not use `using namespace` statements; the full names of types are always required.
- Rules for cpp files:
  - Use `using namespace` statement if necessary to prevent from repeating namespace everywhere.

## Basic C++ Library Leveraging

- This project uses C++ 20, you are recommended to use new C++ 20 features aggressively.
- All code should be cross-platform. In case when an OS feature is needed, a Windows version and a Linux version should be prepared in different files, following the `*.Windows.cpp` and `*.Linux.cpp` naming convention, and keep them as small as possible.
- Use tabs for indentation in C++ source code.
- Use double spaces for indentation for JSON or XML embedded in C++ source code.
- Use `auto` to define variables if it is doable. Use `auto&&` when the type is big or when it is a collection type.

## Advanced C++ Coding Rules

- DO NOT make helper functions that are only used once, especially if they are only called in one destructor.
- DO NOT make global variables with types that carry constructors or destructors, even when they are implicit.
  - This could mess up the order of initialization, finalization or memory leak detector.
- DO NOT reset any raw/shared pointer member to nullPTR in destructorS.
- Prefer the latest C++ features (up to C++ 23).
- Prefer template variadic arguments, over hard-coded-counting solutions.


## Keep C++ Code Cross Platform

- All source files must aim for cross platform unless the file name has `.Windows` or `.Linux.`.
- Use FilePath to normalize file path, for file path operations and delimiter access.