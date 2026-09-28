// libstdc++ was compiled with the old headers: it calls stat64, fstat64,
// lseek64... Linking it with this library must still work.
#include <fstream>
#include <iostream>
int
main ()
{
  std::ifstream f ("x", std::ios::binary);
  f.seekg (0, std::ios::end);
  std::cout << f.tellg () << std::endl;
  return 0;
}
