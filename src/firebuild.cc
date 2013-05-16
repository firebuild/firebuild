
#include "fb-messages.pb.h"
#include <string>
#include <iostream>
#include <unistd.h>

using namespace std;

static void usage()
{
  cout << "usage: firebuild <build command>" << endl;
  exit(0);
}

/**
 * Construct a NULL-terminated array of "NAME=VALUE" environment variables
 * for the build command. The returned stings and array must be free()-d.
 *
 * TODO: detect duplicates
 */
static char** get_sanitized_env()
{
  unsigned int i;
  vector<string> env_v;
  char ** ret_env;
  vector<string>::iterator it;

  // TODO get from config files
  const string pass_through_env_vars[][2] = {{"PATH", ""}, {"SHELL", ""}};
  const string preset_env_vars[][2] = {{"FB_SOCKET", "1234"},
				       {"LD_PRELOAD", "libfbintercept.so"},
				       {"LD_LIBRARY_PATH", "src"}};

  cout << "Passing through environment variables:" << endl;
  for (i = 0; i < sizeof(pass_through_env_vars) / (2 * sizeof(string)); i++) {
    env_v.push_back(pass_through_env_vars[i][0] + "="
		    + getenv(pass_through_env_vars[i][0].c_str()));
    cout << " " << env_v.back() << endl;
  }

  cout << endl;

  cout << "Setting preset environment variables:" << endl;
  for (i = 0; i < sizeof(preset_env_vars) / (2 * sizeof(string)); i++) {
    env_v.push_back(preset_env_vars[i][0] + "=" + preset_env_vars[i][1]);
    cout << " " << env_v.back() << endl;
  }

  cout << endl;

  ret_env = static_cast<char**>(malloc(sizeof(char*) * (env_v.size() + 1)));

  it = env_v.begin();
  i = 0;
  while (it != env_v.end()) {
    ret_env[i] = strdup(it->c_str());
    it++;
    i++;
  }
  ret_env[i] = NULL;

  return ret_env;
}


int main(int argc, char* argv[]) {

  int i;
  int ret;
  char* argv_exec[argc + 1];
  char** env_exec;
  char* env_str;

  if (argc < 2) {
    usage();
  }

  // Verify that the version of the ProtoBuf library that we linked against is
  // compatible with the version of the headers we compiled against.
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  // create and execute build command
  for (i = 0; i < argc; i++) {
    argv_exec[i] = argv[i + 1];
  }
  argv_exec[i] = NULL;

  env_exec = get_sanitized_env();

  ret = execvpe(argv[1], argv_exec, env_exec);

  // clean up everything
  for (i = 0; NULL != (env_str = env_exec[i]); i++) {
    free(env_str);
  }
  free(env_exec);

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();

  return ret;
}
