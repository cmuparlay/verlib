#include <string>
#include <iostream>
#include <ctype.h>

#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/io.h>

#include <verlib/verlib.h>

#include "parse_command_line.h"
#include "timer_loops.h"

#define CHARS

#ifdef CHARS
using K = parlay::chars;
#else
using K = std::string;
#endif

template <typename K>
struct RadixString {
  static int length(K key) { return key.size()+1;}
  static int get_byte(K key, int pos) {
    return (pos >= key.size()) ? 0 : key[pos];}
};

using V = unsigned long;

#include "set.h"

int main(int argc, char* argv[]) {
    commandLine P(argc,argv,"[-r <rounds>] [-threads <num threads>] [-u <update percent>] [-mfind <multifind percent>] [-rs <multifind size>] [-verbose] [-shuffle] [-stats] [-no_check] <filename>");

#ifdef RADIX
    using SetType = ordered_map<K,V,RadixString<K>>;
#elif HASH
    using SetType = unordered_map<K,V,parlay::hash<K>>;
#else // Comparison
    using SetType = ordered_map<K,V>;
#endif

  auto filename = P.getArgument(0);
  
  auto str = parlay::file_map(filename);
  if (str.size() == 0) {
    std::cout << "bad filename: " << filename << std::endl;
    return 1;
  }
  
  auto b = parlay::random_shuffle(parlay::tokens(str, [] (char c) {return !isalnum(c);}));
#ifdef CHARS
  auto a = std::move(b);
#else
  auto a = parlay::map(std::move(b), [] (const parlay::chars& x) {
			    return std::string(x.begin(), x.end());});
#endif
  auto unique = parlay::random_shuffle(parlay::remove_duplicates(a));
  std::cout << "total strings = " << a.size()
	    << ", unique strings = " << unique.size() << std::endl;

  run_mixed_operations<SetType>(a, unique, 0.0, P);
}
