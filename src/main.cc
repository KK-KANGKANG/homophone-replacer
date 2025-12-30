// main.cc
//
// Copyright (c)  2025  Xiaomi Corporation
//
// 独立的拼音词组替换工具主程序

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <iomanip>
#include <locale.h>
#include <cstdlib>
#include <fstream> 

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "homophone-replacer.h"

using namespace hr_standalone;

void PrintUsage() {
  std::cout << "Usage: homophone-replacer-standalone [options]\n\n";
  std::cout << "Options:\n";
  std::cout << "  --text TEXT           Input text for homophone replacement\n";
  std::cout << "  --add-rule K=V       Add runtime rule: pinyin=word (repeatable)\n";
  std::cout << "  --del-rule K         Delete runtime rule by pinyin key (repeatable)\n";
  std::cout << "  --rules-file FILE    Load rules from file, each line K=V\n";
  std::cout << "  --save-rules FILE    Save added rules (K=V) to file\n";
  std::cout << "  --debug               Show debug information\n";
  std::cout << "  --help, -h            Show this help message\n\n";
  std::cout << "Examples:\n";
  std::cout << "  ./homophone-replacer-standalone --text \"他想知道这个问题的答案\"\n";
  std::cout << "  ./homophone-replacer-standalone --text \"他想知道答案\" --debug\n\n";
  std::cout << "Note: Data files should be placed in ./data/hr-files/ directory\n";
}

// 简单的命令行参数解析
class ArgumentParser {
public:
  ArgumentParser(int argc, char* argv[]) {
    // 设置默认固定路径
    lexicon = "./data/hr-files/lexicon.txt";
    rule_fsts = "./data/hr-files/replace.fst";
    
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      
      if (arg == "--help" || arg == "-h") {
        show_help = true;
      } else if (arg == "--debug") {
        debug = true;
      } else if (arg == "--add-rule" && i + 1 < argc) {
        add_rules.push_back(argv[++i]);
      } else if (arg == "--del-rule" && i + 1 < argc) {
        del_rules.push_back(argv[++i]);
      } else if (arg == "--rules-file" && i + 1 < argc) {
        rules_file = argv[++i];
      } else if (arg == "--text" && i + 1 < argc) {
        text = argv[++i];
      } else if (arg == "--save-rules" && i + 1 < argc) {
        save_rules = argv[++i];
      } else {
        std::cerr << "Unknown argument: " << arg << std::endl;
        exit(1);
      }
    }
  }

#ifdef _WIN32
  // Windows: 使用宽字符参数，转换为 UTF-8，避免中文参数乱码
  explicit ArgumentParser(const std::vector<std::string> &args) {
    lexicon = "./data/hr-files/lexicon.txt";
    rule_fsts = "./data/hr-files/replace.fst";

    for (size_t i = 1; i < args.size(); ++i) {
      const std::string &arg = args[i];
      if (arg == "--help" || arg == "-h") {
        show_help = true;
      } else if (arg == "--debug") {
        debug = true;
      } else if (arg == "--add-rule" && i + 1 < args.size()) {
        add_rules.push_back(args[++i]);
      } else if (arg == "--del-rule" && i + 1 < args.size()) {
        del_rules.push_back(args[++i]);
      } else if (arg == "--rules-file" && i + 1 < args.size()) {
        rules_file = args[++i];
      } else if (arg == "--text" && i + 1 < args.size()) {
        text = args[++i];
      } else if (arg == "--save-rules" && i + 1 < args.size()) {
        save_rules = args[++i];
      } else {
        std::cerr << "Unknown argument: " << arg << std::endl;
        exit(1);
      }
    }
  }
#endif

  bool IsValid() const {
    if (show_help) return true;
    
    if (text.empty()) {
      std::cerr << "Error: --text parameter is required\n";
      return false;
    }
    
    return true;
  }

  std::string text;
  std::string lexicon;
  std::string rule_fsts;
  std::vector<std::string> add_rules;
  std::vector<std::string> del_rules;
  std::string rules_file;
  std::string save_rules;
  bool debug = false;
  bool show_help = false;
};

// 设置控制台编码的函数
void SetConsoleEncoding() {
#ifdef _WIN32
  // 设置控制台代码页为UTF-8
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  
  // 设置控制台输出模式
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD dwMode = 0;
  GetConsoleMode(hOut, &dwMode);
  dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(hOut, dwMode);
  
  // 设置locale为UTF-8
  setlocale(LC_ALL, ".UTF8");
  
  // 强制刷新控制台
  system("chcp 65001 >nul");
#endif
}

int main(int argc, char* argv[]) {
  // 设置控制台编码
  SetConsoleEncoding();

#ifdef _WIN32
  // Windows: 始终使用宽字符参数转换为 UTF-8 后解析，避免中文参数丢失/乱码
  int wargc = 0;
  LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  ArgumentParser args = [&]() -> ArgumentParser {
    if (!wargv) {
      return ArgumentParser(argc, argv);
    }
    std::vector<std::string> utf8_args;
    utf8_args.reserve(wargc);
    auto WideToUtf8 = [](const std::wstring &ws) -> std::string {
      if (ws.empty()) return std::string();
      int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), NULL, 0, NULL, NULL);
      std::string strTo(size_needed, 0);
      WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &strTo[0], size_needed, NULL, NULL);
      return strTo;
    };
    for (int i = 0; i < wargc; ++i) {
      utf8_args.emplace_back(WideToUtf8(wargv[i]));
    }
    LocalFree(wargv);
    return ArgumentParser(utf8_args);
  }();
#else
  ArgumentParser args(argc, argv);
#endif
  
  // 如果没有提供参数，使用默认的测试配置
  if (argc == 1) {
    std::cout << "No arguments provided, using default test configuration...\n";
    args.text = "他想知道玄界芯片问题的答案，弓投安装是不错的，机载船感器是什么？";
    args.debug = true;
    
    std::cout << "Default configuration:\n";
    std::cout << "  Text: " << args.text << "\n";
    std::cout << "  Lexicon: " << args.lexicon << "\n";
    std::cout << "  Rules: " << args.rule_fsts << "\n\n";
  }
  
  if (args.show_help) {
    PrintUsage();
    return 0;
  }
  
  if (!args.IsValid()) {
    std::cerr << "\nUse --help for detailed usage\n";
    return -1;
  }

  std::cout << "Initializing Homophone Replacer...\n";
  
  // 创建HomophoneReplacer配置
  HomophoneReplacerConfig hr_config(
    args.lexicon, 
    args.rule_fsts,
    args.debug
  );
  hr_config.add_rules = std::move(args.add_rules);
  hr_config.del_rules = std::move(args.del_rules);
  hr_config.rules_file = std::move(args.rules_file);
  
  if (args.debug) {
    std::cout << "Configuration:\n";
    std::cout << "  " << hr_config.ToString() << "\n";
  }

  // 初始化替换器
  const auto begin = std::chrono::steady_clock::now();
  
  try {
    HomophoneReplacer replacer(hr_config);
    
    const auto init_end = std::chrono::steady_clock::now();
    const float init_seconds = 
        std::chrono::duration_cast<std::chrono::milliseconds>(init_end - begin)
            .count() / 1000.0f;
            
    std::cout << "Initialization completed, time: " << init_seconds << "s\n\n";
    
    // 执行文本替换
    std::cout << "Original text: " << args.text << "\n";
    
    const auto process_begin = std::chrono::steady_clock::now();
    std::string result = replacer.Apply(args.text);
    const auto process_end = std::chrono::steady_clock::now();
    
    const double process_seconds = 
        std::chrono::duration<double>(process_end - process_begin).count();
    
    std::cout.setf(std::ios::fixed);
    std::cout << std::setprecision(4);
    std::cout << "Result: " << result << "\n";
    std::cout << "Processing time: " << process_seconds << "s\n";
    
    // 计算总耗时
    const auto total_end = std::chrono::steady_clock::now();
    const double total_seconds = 
        std::chrono::duration<double>(total_end - begin).count();
            
    std::cout << "Total time: " << total_seconds << "s\n";
    // 可选：保存新增规则到文件（格式 K=V）
    if (!args.save_rules.empty() && !hr_config.add_rules.empty()) {
      std::ofstream ofs(args.save_rules, std::ios::out | std::ios::trunc);
      if (ofs) {
        for (const auto &kv : hr_config.add_rules) ofs << kv << "\n";
        std::cout << "Saved rules to: " << args.save_rules << "\n";
      } else {
        std::cerr << "Failed to write rules file: " << args.save_rules << "\n";
      }
    }
    
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    std::cerr << "\nPlease check if the following files exist:\n";
    std::cerr << "1. Lexicon file: " << args.lexicon << "\n";
    std::cerr << "2. FST rules file: " << args.rule_fsts << "\n";
    return -1;
  }

  return 0;
}
