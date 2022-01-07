#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
# define NOMINMAX
#endif
#include <windows.h>

#include <stdexcept>
#include <sstream>

#include "ModelScene.h"

/* åxçê (C28251) ó}êßÇÃÇΩÇﬂ SAL íçéﬂÇïtó^ */
int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR /*cmdline*/,
    _In_ int /*nCmdShow*/)
{
    ModelScene theApp;
    return theApp.Run();
}

