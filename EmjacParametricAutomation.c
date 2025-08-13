#define PRO_USE_PROTO 1

#include "utility.h"
#include "TabFileSelection.h"



static uiCmdAccessState StarterAppAccess()
  {
    return ACCESS_AVAILABLE;
  }

int user_initialize()
  {
  uiCmdCmdId nButtonID;
  ProGenericMsg(L"EmjacParametricAutomation v1.0.0 loaded...");
  ProCmdActionAdd("StarterAppAction", (uiCmdCmdActFn) esMenu, uiProe2ndImmediate, (uiCmdAccessFn) StarterAppAccess, PRO_B_TRUE, PRO_B_TRUE, &nButtonID);
  ProMenubarmenuPushbuttonAdd("Utilities", "StarterAppAction", "EmjacParametricAutomation EmjacParametricAutomation", "EmjacParametricAutomation EmjacParametricAutomation", "Utilities.psh_util_pref", PRO_B_FALSE, nButtonID, wMsgFile);
  return PRO_TK_NO_ERROR;
  }

void user_terminate()
  {
  }
