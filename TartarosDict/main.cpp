#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <set>
#include <winhttp.h>
#include "resource.h"
#pragma comment(lib, "winhttp.lib")


using namespace std;
HINSTANCE g_hInst;
WNDPROC oldEditProc;

HWND hEdit;
HWND hButton;
HWND hList;

const wchar_t* HOST = L"raw.githubusercontent.com";

const wchar_t* PATH = L"/Kimdakna/TartarosDict/main/x64/Release/unitdict.txt";

struct UnitInfo
{
    std::wstring original;      // 원문
    std::wstring translated;    // 번역명
    std::vector<std::wstring> aliases;
};

std::vector<UnitInfo> g_units;
std::vector<UnitInfo*> g_searchResult;
std::wstring g_DictionaryVersion;

std::string HttpGet(const wchar_t* host,const wchar_t* path)
{
    std::string result;

    HINTERNET hSession =
        WinHttpOpen(L"TartarosDict/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);

    if (!hSession)
        return result;

    HINTERNET hConnect =
        WinHttpConnect(hSession,host,INTERNET_DEFAULT_HTTPS_PORT,0);

    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest =
        WinHttpOpenRequest(
            hConnect,
            L"GET",
            path,
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);

    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    BOOL ok =
        WinHttpSendRequest(
            hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0);

    if (ok)
        ok = WinHttpReceiveResponse(hRequest,NULL);

    if (ok)
    {
        DWORD size = 0;

        do
        {
            WinHttpQueryDataAvailable(hRequest,&size);

            if (size == 0)
                break;

            std::vector<char> buffer(size);

            DWORD read = 0;

            WinHttpReadData(hRequest,buffer.data(),size,&read);

            result.append(buffer.data(),read);

        } while (size > 0);
    }
    if (!ok)
    {
        DWORD err = GetLastError();

        wchar_t buf[128];

        wsprintf(
            buf,
            L"WinHTTP 오류 : %d",
            err);

        MessageBox(
            NULL,
            buf,
            L"Error",
            MB_OK);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

std::wstring UTF8ToWide(const std::string& str)
{
    if (str.empty())
        return L"";

    int len = MultiByteToWideChar(
        CP_UTF8,
        0,
        str.c_str(),
        -1,
        NULL,
        0);

    std::wstring result(len - 1, L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        str.c_str(),
        -1,
        &result[0],
        len);

    return result;
}

std::wstring Trim(const std::wstring& s)
{
    size_t start = s.find_first_not_of(L" \t");
    if (start == std::wstring::npos)
        return L"";

    size_t end = s.find_last_not_of(L" \t");

    return s.substr(start, end - start + 1);
}

std::wstring TrimVersion(std::wstring str)
{
    while (!str.empty() && iswspace(str.front()))
    {
        str.erase(str.begin());
    }


    while (!str.empty() && iswspace(str.back()))
    {
        str.pop_back();
    }

    return str;
}

std::wstring ToLower(std::wstring str)
{
    std::transform(
        str.begin(),
        str.end(),
        str.begin(),
        towlower);

    return str;
}

std::wstring RemoveSpaces(std::wstring str)
{
    str.erase(
        std::remove_if(
            str.begin(),
            str.end(),
            [](wchar_t c)
    {
        return iswspace(c);
    }),
        str.end());

    return str;
}

bool ParseLine(const std::wstring& line, UnitInfo& unit)
{
    int comma1 = -1;
    int comma2 = -1;
    bool alias = false;

    for (int i = 0; i < (int)line.size(); i++)
    {
        if (line[i] == L'<')
            alias = true;
        else if (line[i] == L'>')
            alias = false;
        else if (line[i] == L',' && !alias)
        {
            if (comma1 == -1)
                comma1 = i;
            else
            {
                comma2 = i;
                break;
            }
        }
    }

    if (comma1 == -1 || comma2 == -1)
        return false;

    unit.original =
        Trim(line.substr(0, comma1));

    unit.translated =
        Trim(line.substr(comma1 + 1,
            comma2 - comma1 - 1));

    std::wstring aliasPart =
        Trim(line.substr(comma2 + 1));

    if (aliasPart.size() >= 2)
    {
        if (aliasPart.front() == L'<')
            aliasPart.erase(aliasPart.begin());

        if (!aliasPart.empty() &&
            aliasPart.back() == L'>')
            aliasPart.pop_back();

        std::wstringstream ss(aliasPart);

        std::wstring item;

        while (getline(ss, item, L','))
        {
            item = Trim(item);

            if (!item.empty())
                unit.aliases.push_back(item);
        }
    }

    return true;
}

bool Contains(const std::wstring& target, const std::wstring& keyword)
{
    std::wstring t = RemoveSpaces(ToLower(target));
    std::wstring k = RemoveSpaces(ToLower(keyword));

    return t.find(k) != std::wstring::npos;
}

std::wstring GetLocalVersion()
{
    std::ifstream file("unitdict.txt", std::ios::binary);

    if (!file.is_open())
        return L"";

    std::string line;

    if (!std::getline(file, line))
        return L"";

    if (line.size() >= 3 &&
        (unsigned char)line[0] == 0xEF &&
        (unsigned char)line[1] == 0xBB &&
        (unsigned char)line[2] == 0xBF)
    {
        line.erase(0, 3);
    }

    std::wstring wline = UTF8ToWide(line);

    const std::wstring prefix = L"#VERSION=";

    if (wline.rfind(prefix, 0) == 0)
    {
        return TrimVersion(
            wline.substr(prefix.length())
        );
    }

    return L"";
}

std::wstring GetGithubVersion()
{
    std::string data = HttpGet(HOST, PATH);

    if (data.empty())
        return L"";

    std::istringstream ss(data);

    std::string firstLine;

    getline(ss, firstLine);

    if (!firstLine.empty() &&
        firstLine.back() == '\r')
    {
        firstLine.pop_back();
    }

    if (firstLine.size() >= 3 &&
        (unsigned char)firstLine[0] == 0xEF &&
        (unsigned char)firstLine[1] == 0xBB &&
        (unsigned char)firstLine[2] == 0xBF)
    {
        firstLine.erase(0, 3);
    }

    std::wstring w =
        UTF8ToWide(firstLine);

    if (w.rfind(L"#VERSION=", 0) == 0)
    {
        return TrimVersion(
            w.substr(9)
        );
    }

    return L"";
}

bool CheckFileVersion(const wchar_t* file, const std::wstring& version)
{
    std::ifstream fs(file, std::ios::binary);

    if (!fs.is_open())
        return false;

    std::string line;
    getline(fs, line);

    fs.close();
    std::wstring wline = UTF8ToWide(line);

    return wline == L"#VERSION=" + version;
}

bool DownloadFile(const wchar_t* host, const wchar_t* path, const wchar_t* savePath)
{
    std::string data = HttpGet(host, path);

    if (data.empty())
        return false;

    std::ofstream file(savePath, std::ios::binary);

    if (!file.is_open())
        return false;

    file.write(data.data(), data.size());
    file.close();

    return true;
}

bool ReplaceDictionaryFile()
{
    // 기존 백업 삭제
    DeleteFile(L"unitdict_backup.txt");


    // 기존 파일 백업
    if (!MoveFile(L"unitdict.txt", L"unitdict_backup.txt"))
    {
        return false;
    }


    // tmp -> 실제 파일
    if (!MoveFile(L"unitdict.tmp", L"unitdict.txt"))
    {
        // 실패하면 복구
        MoveFile(
            L"unitdict_backup.txt",
            L"unitdict.txt");

        return false;
    }

    return true;
}

bool UpdateDictionary()
{
    const wchar_t* tempFile = L"unitdict.tmp";

    std::wstring githubVersion = GetGithubVersion();

    if (!DownloadFile(HOST,PATH,tempFile))
    {
        return false;
    }

    if (!CheckFileVersion(tempFile,githubVersion))
    {
        DeleteFile(tempFile);
        return false;
    }

    if (!ReplaceDictionaryFile())
    {
        DeleteFile(tempFile);
        return false;
    }


    return true;
}

void ClearDictionary()
{
    g_units.clear();
    g_searchResult.clear();
}

bool LoadDictionary()
{
    ClearDictionary();

    std::ifstream file("unitdict.txt", std::ios::binary);

    if (!file.is_open())
        return false;

    g_units.clear();

    std::string line;

    while (getline(file, line))
    {
        std::wstring wline = UTF8ToWide(line);

        if (wline.rfind(L"#VERSION=", 0) == 0)
        {
            g_DictionaryVersion = wline.substr(9);
            continue;
        }
        if (!line.empty() &&
            line.back() == '\r')
            line.pop_back();

        // UTF-8 BOM 제거
        if (line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
        {
            line.erase(0, 3);
        }

        UnitInfo unit;

        if (ParseLine(wline, unit))
            g_units.push_back(unit);
    }

    return true;
}

void CheckUpdate()
{
    std::wstring localVersion =
        GetLocalVersion();


    std::wstring githubVersion =
        GetGithubVersion();


    // GitHub 접속 실패
    if (githubVersion.empty())
    {
        return;
    }


    // 버전 동일
    if (localVersion == githubVersion)
    {
        return;
    }


    // 버전 다름
    wchar_t msg[256];

    wsprintf(
        msg,
        L"새 버전 발견\n\n현재 : %s\n최신 : %s",
        localVersion.c_str(),
        githubVersion.c_str());


    if (UpdateDictionary())
    {
        ClearDictionary();

        LoadDictionary();

        MessageBox(
            NULL,
            L"사전 업데이트 완료",
            L"Update",
            MB_OK);
    }
    else
    {
        MessageBox(
            NULL,
            L"사전 업데이트 실패",
            L"Update",
            MB_OK);
    }
}

bool CopyToClipboard(const std::wstring& text)
{
    if (!OpenClipboard(NULL))
        return false;

    EmptyClipboard();

    size_t size = (text.size() + 1) * sizeof(wchar_t);

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);

    if (!hMem)
    {
        CloseClipboard();
        return false;
    }

    void* ptr = GlobalLock(hMem);

    memcpy(ptr, text.c_str(), size);

    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();

    return true;
}

void SearchUnit(const std::wstring& keyword)
{
    g_searchResult.clear();

    std::set<UnitInfo*> added;

    // ----------------------------
    // 번역명 검색
    // ----------------------------

    for (auto& unit : g_units)
    {
        if (Contains(
            unit.translated,
            keyword))
        {
            g_searchResult.push_back(&unit);
            added.insert(&unit);
        }
    }


    // 가나다 순 정렬
    std::sort(
        g_searchResult.begin(),
        g_searchResult.end(),
        [](UnitInfo* a, UnitInfo* b)
    {
        return a->translated
            < b->translated;
    });

    // ----------------------------
    // 별명 검색
    // ----------------------------

    std::vector<UnitInfo*> aliasResult;


    for (auto& unit : g_units)
    {
        if (added.count(&unit))
            continue;


        for (auto& alias : unit.aliases)
        {
            if (Contains(alias, keyword))
            {
                aliasResult.push_back(&unit);
                break;
            }
        }
    }

    // 별명 검색 결과 가나다 정렬
    std::sort(
        aliasResult.begin(),
        aliasResult.end(),
        [](UnitInfo* a, UnitInfo* b)
    {
        return a->translated
            < b->translated;
    });

    // 뒤에 추가
    for (auto item : aliasResult)
    {
        g_searchResult.push_back(item);
    }
}

void UpdateList()
{
    SendMessage(
        hList,
        LB_RESETCONTENT,
        0,
        0);


    for (auto unit : g_searchResult)
    {
        SendMessage(
            hList,
            LB_ADDSTRING,
            0,
            (LPARAM)unit->translated.c_str());
    }
}

void ExecuteSearch()
{
    wchar_t buffer[256];

    GetWindowText(
        hEdit,
        buffer,
        256);

    SearchUnit(buffer);

    UpdateList();
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN)
    {
        if (wParam == VK_RETURN)
        {
            ExecuteSearch();
            return 0;
        }
    }

    return CallWindowProc(
        oldEditProc,
        hwnd,
        msg,
        wParam,
        lParam);
}

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    LPSTR,
    int nCmdShow)
{
    g_hInst = hInstance;

    WNDCLASS wc = {};

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"UnitDictionary";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        L"유닛 사전",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        700,
        500,
        NULL,
        NULL,
        hInstance,
        NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        hEdit = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            10,
            10,
            420,
            28,
            hwnd,
            (HMENU)IDC_EDIT_SEARCH,
            g_hInst,
            NULL);

        hButton = CreateWindow(
            L"BUTTON",
            L"검색",
            WS_CHILD | WS_VISIBLE,
            440,
            10,
            100,
            28,
            hwnd,
            (HMENU)IDC_BUTTON_SEARCH,
            g_hInst,
            NULL);

        hList = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            NULL,
            WS_CHILD |
            WS_VISIBLE |
            WS_VSCROLL |
            LBS_NOTIFY,
            10,
            50,
            660,
            390,
            hwnd,
            (HMENU)IDC_LIST_RESULT,
            g_hInst,
            NULL);

        CheckUpdate();

        if (!LoadDictionary())
        {
            MessageBox(
                hwnd,
                L"unitdict.txt를 찾을 수 없습니다.",
                L"오류",
                MB_ICONERROR);
        }
        oldEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    }
    break;
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_BUTTON_SEARCH:
            ExecuteSearch();
            break;
        case IDC_LIST_RESULT:
        {
            if (HIWORD(wParam) == LBN_DBLCLK)
            {
                int index = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);

                if (index != LB_ERR)
                {
                    UnitInfo* unit = g_searchResult[index];

                    CopyToClipboard(unit->original);

                    // MessageBox(hwnd,L"복사 완료.",L"완료",MB_OK);
                }
            }
        }
        break;
        }
    }
    break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}