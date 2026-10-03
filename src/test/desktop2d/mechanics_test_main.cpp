#include <exception>
#include <iostream>
void runGameplay2dTests();
int main()
{
    try { runGameplay2dTests(); std::cout << "Gameplay2D tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
