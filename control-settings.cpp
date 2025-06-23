#include <windows.h>
#include <winuser.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>

class KeyboardController {
private:
    HHOOK hKeyboardHook;
    bool gameMode;
    HWND powershellWindow;
    
    // Navigation history for backspace undo
    std::vector<POINT> navigationHistory;
    
public:
    KeyboardController() : hKeyboardHook(nullptr), gameMode(true), powershellWindow(nullptr) {}
    
    bool Initialize() {
        // Set up global keyboard hook
        hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(nullptr), 0);
        if (!hKeyboardHook) {
            std::cerr << "Failed to install keyboard hook" << std::endl;
            return false;
        }
        
        // Create persistent PowerShell window
        CreatePersistentPowerShell();
        
        // Add to startup registry
        AddToStartup();
        
        return true;
    }
    
    void Cleanup() {
        if (hKeyboardHook) {
            UnhookWindowsHookEx(hKeyboardHook);
        }
    }
    
    static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode >= 0 && wParam == WM_KEYDOWN) {
            KBDLLHOOKSTRUCT* pKeyStruct = (KBDLLHOOKSTRUCT*)lParam;
            return GetInstance()->ProcessKeyPress(pKeyStruct->vkCode, pKeyStruct->flags);
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    
    LRESULT ProcessKeyPress(DWORD vkCode, DWORD flags) {
        if (!gameMode) {
            // Only handle Caps Lock in normal mode
            if (vkCode == VK_CAPITAL) {
                gameMode = true;
                return 1; // Block the key
            }
            return CallNextHookEx(hKeyboardHook, 0, WM_KEYDOWN, 0);
        }
        
        // Game mode active - process custom controls
        bool shiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;
        bool winPressed = GetAsyncKeyState(VK_LWIN) & 0x8000;
        
        switch (vkCode) {
            case VK_CAPITAL: // Caps Lock - toggle to normal mode
                gameMode = false;
                return 1;
                
            case 'W': // Forward/Up
                if (shiftPressed) {
                    ScrollPage(0, -3); // Scroll up
                } else {
                    SimulateKey(VK_UP);
                }
                return 1;
                
            case 'A': // Left
                if (shiftPressed) {
                    ScrollPage(-3, 0); // Scroll left
                } else {
                    SimulateKey(VK_LEFT);
                }
                return 1;
                
            case 'S': // Down
                if (shiftPressed) {
                    ScrollPage(0, 3); // Scroll down
                } else {
                    SimulateKey(VK_DOWN);
                }
                return 1;
                
            case 'D': // Right
                if (shiftPressed) {
                    ScrollPage(3, 0); // Scroll right
                } else {
                    SimulateKey(VK_RIGHT);
                }
                return 1;
                
            case VK_SPACE: // Select
                SimulateKey(VK_RETURN);
                return 1;
                
            case VK_ESCAPE: // Minimize page
                MinimizeCurrentWindow();
                return 1;
                
            case VK_DELETE:
                if (shiftPressed) {
                    // Shift + Delete - close Firefox tab
                    CloseFirefoxTab();
                } else {
                    // Delete alone - exit current page
                    SimulateKey(VK_F4, true); // Alt+F4
                }
                return 1;
                
            case VK_BACK: // Backspace - undo navigation
                UndoNavigation();
                return 1;
                
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT:
                // Arrow keys for smart navigation
                SmartNavigate(vkCode);
                return 1;
                
            case VK_LWIN:
                if (shiftPressed) {
                    // Win + Shift - minimize all tabs and return to desktop
                    MinimizeAllWindows();
                } else {
                    // Win alone - exit all tabs and return to desktop
                    CloseAllTabsAndShowDesktop();
                }
                return 1;
        }
        
        return CallNextHookEx(hKeyboardHook, 0, WM_KEYDOWN, 0);
    }
    
    void SimulateKey(DWORD vkCode, bool useAlt = false) {
        INPUT inputs[4] = {};
        int inputCount = 0;
        
        if (useAlt) {
            inputs[inputCount].type = INPUT_KEYBOARD;
            inputs[inputCount].ki.wVk = VK_MENU;
            inputCount++;
        }
        
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = vkCode;
        inputCount++;
        
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = vkCode;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
        inputCount++;
        
        if (useAlt) {
            inputs[inputCount].type = INPUT_KEYBOARD;
            inputs[inputCount].ki.wVk = VK_MENU;
            inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
            inputCount++;
        }
        
        SendInput(inputCount, inputs, sizeof(INPUT));
    }
    
    void ScrollPage(int x, int y) {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = y * WHEEL_DELTA;
        SendInput(1, &input, sizeof(INPUT));
        
        if (x != 0) {
            input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
            input.mi.mouseData = x * WHEEL_DELTA;
            SendInput(1, &input, sizeof(INPUT));
        }
    }
    
    void MinimizeCurrentWindow() {
        HWND hwnd = GetForegroundWindow();
        if (hwnd) {
            ShowWindow(hwnd, SW_MINIMIZE);
        }
    }
    
    void CloseFirefoxTab() {
        // Ctrl+W to close Firefox tab
        INPUT inputs[4] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = 'W';
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = 'W';
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wVk = VK_CONTROL;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        
        SendInput(4, inputs, sizeof(INPUT));
    }
    
    void SmartNavigate(DWORD direction) {
        // Store current cursor position for undo
        POINT currentPos;
        GetCursorPos(&currentPos);
        navigationHistory.push_back(currentPos);
        
        // Simple implementation - move cursor and try to find clickable elements
        POINT newPos = currentPos;
        int moveDistance = 50;
        
        switch (direction) {
            case VK_UP: newPos.y -= moveDistance; break;
            case VK_DOWN: newPos.y += moveDistance; break;
            case VK_LEFT: newPos.x -= moveDistance; break;
            case VK_RIGHT: newPos.x += moveDistance; break;
        }
        
        SetCursorPos(newPos.x, newPos.y);
    }
    
    void UndoNavigation() {
        if (!navigationHistory.empty()) {
            POINT lastPos = navigationHistory.back();
            navigationHistory.pop_back();
            SetCursorPos(lastPos.x, lastPos.y);
        }
    }
    
    void MinimizeAllWindows() {
        // Win+D equivalent
        INPUT inputs[4] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_LWIN;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = 'D';
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = 'D';
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wVk = VK_LWIN;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        
        SendInput(4, inputs, sizeof(INPUT));
    }
    
    void CloseAllTabsAndShowDesktop() {
        // Alt+F4 to close current window, then show desktop
        SimulateKey(VK_F4, true);
        Sleep(100);
        MinimizeAllWindows();
    }
    
    void CreatePersistentPowerShell() {
        // Create a PowerShell window that stays open
        std::string command = "powershell.exe -WindowStyle Normal -NoExit -Command \"& {$Host.UI.RawUI.WindowTitle='Persistent Terminal'; while($true){try{$cmd=Read-Host 'PS>';if($cmd -eq 'exit'){continue}; Invoke-Expression $cmd}catch{Write-Error $_.Exception.Message}}}\"";
        
        STARTUPINFO si = {};
        PROCESS_INFORMATION pi = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_MAXIMIZE;
        
        CreateProcess(nullptr, (LPSTR)command.c_str(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        
        // Store window handle and make it transparent
        Sleep(2000); // Wait for window to appear
        powershellWindow = FindWindow(nullptr, L"Persistent Terminal");
        if (powershellWindow) {
            SetWindowLong(powershellWindow, GWL_EXSTYLE, GetWindowLong(powershellWindow, GWL_EXSTYLE) | WS_EX_LAYERED);
            SetLayeredWindowAttributes(powershellWindow, 0, 50, LWA_ALPHA); // Very transparent
        }
    }
    
    void AddToStartup() {
        HKEY hKey;
        const char* czStartupPath = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
        
        if (RegOpenKeyEx(HKEY_CURRENT_USER, czStartupPath, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            char szPath[MAX_PATH];
            GetModuleFileName(nullptr, szPath, MAX_PATH);
            
            RegSetValueEx(hKey, "KeyboardController", 0, REG_SZ, (BYTE*)szPath, strlen(szPath) + 1);
            RegCloseKey(hKey);
        }
    }
    
    static KeyboardController* GetInstance() {
        static KeyboardController instance;
        return &instance;
    }
    
    void MonitorPowerShell() {
        // Monitor and restart PowerShell if closed
        while (true) {
            if (powershellWindow && !IsWindow(powershellWindow)) {
                CreatePersistentPowerShell();
            }
            Sleep(5000); // Check every 5 seconds
        }
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    KeyboardController* controller = KeyboardController::GetInstance();
    
    if (!controller->Initialize()) {
        MessageBox(nullptr, L"Failed to initialize keyboard controller", L"Error", MB_OK);
        return 1;
    }
    
    // Hide console window
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow) {
        ShowWindow(consoleWindow, SW_HIDE);
    }
    
    // Start PowerShell monitoring thread
    CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
        ((KeyboardController*)param)->MonitorPowerShell();
        return 0;
    }, controller, 0, nullptr);
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    controller->Cleanup();
    return 0;
}
