#ifndef V8_JSTRACE_H_
#define V8_JSTRACE_H_

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <climits>
#include <string>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <queue>
#include <sstream>
#include <fstream>
#include "include/v8.h"
#include "src/objects.h"

namespace v8{
    namespace internal{
	std::string tolower(std::string);
	std::string preparePathForSavingFile(std::string);
	
	class JSTrace{
	public:
	    static void setFileName(char*);
	    static std::string getFileName();
	    static void setSourceCode(uint16_t*, int);
	    static void saveSourceCode(std::string);
	    static uint16_t* getSourceCode();
	private:
	    static std::string fileName_;
	    static uint16_t* sourceCode_;
	};
	
	class LookBackInfo{
	public:
	    void print(std::string = "");
	    bool equalsToArg1(std::vector<std::pair<std::string, long> >);
	    bool equalsToArg2(std::vector<std::pair<std::string, long> >);
	    bool equalsToOp(std::string);
	    std::vector<std::pair<std::string, long> > convertToArray();
	    void optimize_find_duplicate_string(std::vector<std::pair<std::string, long> > &);
	    std::pair<std::string, long> op_;
	    std::vector<std::pair<std::string, long> > arg1_;
	    std::vector<std::pair<std::string, long> > arg2_;
	};

	class LookBackMap{
	public:
	    static void print(std::string, std::string, std::string = "");
	    static LookBackInfo* find(std::string);
	    static void remove(std::string);
	    static bool contains(std::string);

	    static void append(std::string, Handle<Object>, Handle<Object>, Handle<Object>);
	    static void append(std::string, Handle<Object>, Handle<Object>);
	    static void assign(std::string target, std::string from);
	    static void printStatistic(std::string path = "");
	    static void init();
	    static void not_in_eval();
	    static void in_eval();

	    static void statInsert(std::string);

	private:
	    static std::unordered_map<std::string, LookBackInfo> nbmap_;
	    static std::unordered_map<std::string, long long> stat_;
	    static bool in_eval_;
	};
    }
}

#endif  // V8_JSTRACE_H_
