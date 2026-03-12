#include <iostream>

#include <boost/filesystem.hpp>

int main(int argc, char** argv) {
  // Boost.Filesystem (requires linking)
  std::cout << "Vox server - CWD: " << boost::filesystem::current_path().string() << "\n";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "vox-server - Server side of the Vox messenger\n";
      std::cout << "Usage: vox-server [--help]\n";
      return 0;
    }
  }

  // TODO: Server implementation
  std::cout << "Server not yet implemented.\n";
  return 0;
}
