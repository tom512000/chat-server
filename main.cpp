#include "server.hpp"

int usage ()
{
  std::cerr << "Usage: server <port>" << std::endl;
  return 1;
}

int main (int argc, char * argv [])
{
  if (argc != 2)
    return usage ();

  try
  {
    Server server (std::stoi (argv [1]));
    server.start ();
  }
  catch (std::exception & e)
  {
    std::cerr << e.what () << std::endl;
    return usage ();
  }
  catch (...)
  {
    return usage ();
  }

  return 0;
}

