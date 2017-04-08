#ifndef DROID_BUILD
#define DROID_BUILD

#include <stdio.h>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <unistd.h>
#include <sys/stat.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "libparse/parser.h"
#include <sstream>
#include <assert.h>


thread_local std::map<std::string,std::string> env;


class String:public libparse::ParseTree {
public:
  std::string val;
  String(const char* literal):ParseTree(literal) {
    char found;
    libparse::StringRef out;
    std::stringstream output;
    while(expect(out,found,'$')) {
      output<<(std::string)out;
      assert(expect(out,found,'$'));
      if(!out.count) {
	output<<'$';
      }else {
	if(env.find((std::string)out) != env.end()) {
	  output<<env[(std::string)out];
	}else {
	  const char* localvar = getenv(((std::string)out).data());
	  if(localvar) {
	    output<<localvar;
	  }
	}
      }
    }
    output<<out.ptr;
    val = output.str();
    
  }
  operator std::string() const {
    return val;
  }
  bool operator<(const String& other) const {
    return val<other.val;
  }
};


namespace droidbuild {

  class Target {
  public:
    String name;
    std::vector<std::shared_ptr<Target>> dependencies;
    std::string link_args;
    
    Target(const String& name):name(name) {
      
    }
    void add_dependency(const std::shared_ptr<Target>& dep) {
      dependencies.push_back(dep);
    }
    void build() {
      for(size_t i = 0;i<dependencies.size();i++) {
	dependencies[i]->build();
      }
      _build();
    }
  protected:
    virtual void _build() = 0;
  };
  class LibraryTarget:public Target {
  public:
  LibraryTarget(const String& name):Target(name) {
  }
  protected:
  void _build() {
    std::stringstream link_args;
    for(size_t i = 0;i<dependencies.size();i++) {
      link_args<<dependencies[i]->link_args;
    }
    
    env["link_args"] = link_args.str();
    env["target"] = name.val.c_str();
    String cmd("$ANDROID_NDK$/toolchains/$platform$-4.9/prebuilt/linux-x86_64/bin/$platform$-g++ --shared --sysroot=$ANDROID_NDK$/toolchains/$platform$-4.9/prebuilt/linux-x86_64 -o $build_dir$/$target$.so $link_args$");
    if(system(cmd.val.c_str())) {
      exit(-6);
    }
  }
  };
  
  
  class CppTarget:public Target {
  public:
  CppTarget(const String& name):Target(name) {
  }
  protected:
  void _build() {
    env["filename"] = (std::string)name;
    env["out_filename"] = String("$build_dir$/$filename$.o");
    link_args = env["out_filename"];
    String cmd("$ANDROID_NDK$/toolchains/$platform$-4.9/prebuilt/linux-x86_64/bin/$platform$-g++ -c --sysroot=$ANDROID_NDK$/toolchains/$platform$-4.9/prebuilt/linux-x86_64 $filename$ -o $out_filename$");
    if(system(cmd.val.data())) {
      exit(-5);
    }
  }
  };
  
  
static std::string android_ndk_root;  
static std::string android_sdk_root;

static std::map<std::string,std::shared_ptr<Target>> targets;




static int execbuild(int argc, char** argv) {
  
  
  if(!getenv("ANDROID_NDK")) {
    printf("Please set the ANDROID_NDK environment variable to the root of your Android NDK.\n");
    return -1;
  }
  if(!getenv("ANDROID_SDK")) {
    printf("Please set the ANDROID_SDK environment variable to the root of your Android SDK.\n");
    return -1;
  }
  
  android_ndk_root = getenv("ANDROID_NDK");
  android_sdk_root = getenv("ANDROID_SDK");
  
  std::vector<std::string> toolchains;
  toolchains.push_back("aarch64-linux-android");
  toolchains.push_back("arm-linux-androideabi");
  toolchains.push_back("mips64el-linux-android");
  toolchains.push_back("mipsel-linux-android");
  //toolchains.push_back("x86_64");
  //toolchains.push_back("x86");
  
  std::mutex mtx;
  std::condition_variable evt;
  
  size_t pending = toolchains.size();
  
  size_t toolchains_total = toolchains.size();
  for(size_t i = 0;i<toolchains_total;i++) {
    std::thread m([&](){
      mtx.lock();
      
      std::string toolchain = toolchains[toolchains.size()-1];
      toolchains.pop_back();
      
      mtx.unlock();
      
      env["platform"] = toolchain;
      
      
      env["build_dir"] = String("build-$platform$").val;
      mkdir(String("build-$platform$").val.data(),S_IRUSR | S_IWUSR | S_IXUSR);
      
      //Perform build of all targets
      for(auto bot = targets.begin();bot!= targets.end();bot++) {
	env["target"] = (std::string)bot->first;
	mtx.lock();
	printf("Building target %s for platform %s\n",bot->first.data(),toolchain.data());
	mtx.unlock();
	
	std::shared_ptr<Target> target = bot->second;
	target->build();
	
	mtx.lock();
	printf("Finished building target %s for platform %s\n",bot->first.data(),toolchain.data());
	mtx.unlock();
	
      }
      
      
      
      mtx.lock();
      printf("Built %i of %i targets -- Finished building for platform %s\n",(int)((toolchains_total-pending)+1),(int)toolchains_total,toolchain.data());
      
      pending--;
      if(pending == 0) {
	evt.notify_one();
      }
      mtx.unlock();
    });
    
    m.detach();
  }
  std::unique_lock<std::mutex> l(mtx);
  if(pending) {
    evt.wait(l);
  }
  
  
  return 0;
}


}




static void add_library(const String& name) {
  droidbuild::targets[name] = std::make_shared<droidbuild::LibraryTarget>(name);
}

template<typename... T>
static void add_cpp_files(const String& target_name, T... files) {
  String filenames[] = {files...};
  if(droidbuild::targets.find(target_name) == droidbuild::targets.end()){
      printf("Error. Target %s not found.\n",target_name.val.data());
      exit(-2);
    }else {
    }
    
  std::shared_ptr<droidbuild::Target> target = droidbuild::targets[target_name];
  for(size_t i = 0;i<sizeof(filenames)/sizeof(String);i++) {
    
    target->add_dependency(std::make_shared<droidbuild::CppTarget>(filenames[i]));
  }
  
}



#define START int main(int argc, char** argv) {

#define END return droidbuild::execbuild(argc,argv);}

#endif