// The bounded verifier must expose completion before its caller starts a
// rollback. Pipe output is otherwise buffered until process exit.
#define wmain NativeIslandHostMain
#include "native-island.cpp"
#undef wmain
int wmain(int argc, wchar_t **argv) {
    std::cout << std::unitbuf;
    return NativeIslandHostMain(argc, argv);
}
