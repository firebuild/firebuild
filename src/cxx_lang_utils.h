
#ifndef FIREBUILD_CXX_LANG_UTILS_H
#define FIREBUILD_CXX_LANG_UTILS_H

// From http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml
// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)

#endif
