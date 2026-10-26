// Resolve crash RVAs against a matching local PDB with the installed DIA SDK.
#include <windows.h>
#include <dia2.h>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "Usage: symbolize.exe module.pdb msdia140.dll RVA [RVA ...]\n";
    return 2;
  }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  HMODULE dll = LoadLibraryA(argv[2]);
  if (!dll) return 3;
  auto get_class = reinterpret_cast<HRESULT (WINAPI*)(REFCLSID, REFIID, void**)>(
      GetProcAddress(dll, "DllGetClassObject"));
  if (!get_class) return 4;
  IClassFactory* factory = nullptr;
  IDiaDataSource* source = nullptr;
  HRESULT hr = get_class(CLSID_DiaSource, IID_IClassFactory, reinterpret_cast<void**>(&factory));
  if (SUCCEEDED(hr)) {
    hr = factory->CreateInstance(nullptr, IID_IDiaDataSource, reinterpret_cast<void**>(&source));
    factory->Release();
  }
  if (FAILED(hr)) return 5;
  wchar_t path[MAX_PATH];
  if (!MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, path, MAX_PATH)) return 6;
  hr = source->loadDataFromPdb(path);
  IDiaSession* session = nullptr;
  if (SUCCEEDED(hr)) hr = source->openSession(&session);
  if (FAILED(hr)) { std::cerr << "DIA error 0x" << std::hex << hr << '\n'; return 7; }
  for (int i = 3; i < argc; ++i) {
    const DWORD rva = static_cast<DWORD>(std::strtoul(argv[i], nullptr, 16));
    IDiaSymbol* symbol = nullptr;
    session->findSymbolByRVA(rva, SymTagFunction, &symbol);
    std::cout << "RVA 0x" << std::hex << rva << ": ";
    if (symbol) {
      BSTR name = nullptr;
      DWORD start = 0;
      symbol->get_name(&name);
      symbol->get_relativeVirtualAddress(&start);
      if (name) { std::wcout << name; SysFreeString(name); }
      std::cout << " +0x" << rva - start;
      symbol->Release();
    } else std::cout << "unresolved";
    std::cout << '\n';
  }
  session->Release(); source->Release();
  FreeLibrary(dll); CoUninitialize();
  return 0;
}
