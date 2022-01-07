#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
# define NOMINMAX
#endif
#include <windows.h>

#include <stdexcept>
#include <sstream>

#include "IntersectionScene.h"

/* 警告 (C28251) 抑制のため SAL 注釈を付与 */
int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR /*cmdline*/,
    _In_ int /*nCmdShow*/)
{
    IntersectionScene theApp;
    return theApp.Run();
}

