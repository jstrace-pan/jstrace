#include "jstrace.h"
#include "src/objects-inl.h"
#include "unicode/ustdio.h"

#define PRINT_DEBUG_INFO false
#define SAVE_JS false

// For JSTrace
namespace v8{
    namespace internal{
	std::string JSTrace::fileName_;
	uint16_t* JSTrace::sourceCode_ = NULL;

	std::string tolower(std::string str){
	    for (int i = 0; i < str.length(); i++){
		str[i] = std::tolower(str[i]);
	    }

	    return str;
	}
	
	std::string preparePathForSavingFile(std::string path){
	    int i = 0, pos = path.find('/');
	    std::string newPath;

	    while (pos != -1){
		std::string tmp = path.substr(i, pos - i);
		
		if (tolower(tmp) == "http:" && path[i + 5] == '/' && path[i + 6] == '/'){
		    i = i + 7;
		    pos = i;
		}
		else if (tolower(tmp) == "https:" && path[i + 6] == '/' && path[i + 7] == '/'){
		    i = i + 8;
		    pos = i;
		}
		else{
		    newPath += tmp + '/';
		    i = ++pos;

		    mkdir(newPath.c_str(), S_IRWXU);
		}

		pos = path.find('/', pos);
	    }

	    newPath += path.substr(i);

	    return newPath;
	}

	void JSTrace::saveSourceCode(std::string filePath){
	    if (SAVE_JS && sourceCode_){
	    	std::string newPath = preparePathForSavingFile(filePath);
	    	UFILE* f = u_fopen(newPath.c_str(), "w", NULL, "UTF-8");
	    	u_fputs((UChar*)sourceCode_, f);
	    	u_fclose(f);
	    }
	}
	
	void JSTrace::setFileName(char* fileName){
	    fileName_.clear();
	    fileName_.assign(fileName);
	}

	std::string JSTrace::getFileName(){
	    return fileName_;
	}

	void JSTrace::setSourceCode(uint16_t *sourceCode, int sourceCodeLength){
	    if (sourceCode){
		sourceCode_ = i::NewArray<uint16_t>(sourceCodeLength + 1);
		memcpy(sourceCode_, sourceCode, sourceCodeLength);
	    }
	}

	uint16_t* JSTrace::getSourceCode(){
	    return sourceCode_;
	}
    }
}

namespace v8{
    namespace internal{
	std::unordered_map<std::string, LookBackInfo> LookBackMap::nbmap_;
	std::unordered_map<std::string, long long> LookBackMap::stat_;
	bool LookBackMap::in_eval_ = false;

	void LookBackMap::statInsert(std::string op){
	    std::unordered_map<std::string, long long>::iterator found = stat_.find(op);

	    if (found == stat_.end()){
		stat_.insert({op, 1});
	    }
	    else{
		found->second += 1;
	    }
	}

	std::vector<std::vector<std::string> > generateLevelArray(std::vector<std::pair<std::string, long> > &originalArray){
	    int levelLimit = 12;
	    std::vector<std::vector<std::string> > levelInfo;
	    int level = 0;
	    std::string current = "";
	    // generate level array
	    for (size_t i = 0; i < originalArray.size(); i++){
		if (levelInfo.size() < level + 1 && level <= levelLimit){
		    levelInfo.resize(level + 1);
		}
		if (originalArray[i].first == "("){
		    if (level == levelLimit && current.size() > 0){
			levelInfo[level].push_back("cv=cv");
		    }
		    else if (current.size() > 0 && level < levelLimit){
			levelInfo[level].push_back(current);
		    }
		    current = "";
		    level += 1;
		}
		else if (originalArray[i].first == ")"){
		    if (level == levelLimit && current.size() > 0){
			levelInfo[level].push_back("cv=cv");
		    }
		    else if (current.size() > 0 && level < levelLimit){
			levelInfo[level].push_back(current);
		    }
		    current = "";
		    level -= 1;
		}
		else {
		    if (current == ""){
			current = originalArray[i].first;
		    }
		    else{
			current += " " + originalArray[i].first;
		    }
		}
	    }

	    if (level != 0){
		levelInfo.clear();
	    }

	    return levelInfo;
	}

	bool LookBackInfo::equalsToArg1(std::vector<std::pair<std::string, long> > arg){
	    if (arg.size() != arg1_.size()){
		return false;
	    }
	    bool result = true;
	    for (size_t i = 0; i < arg.size(); i++){
		if (arg[i].first != arg1_[i].first){
		    result = false;
		    break;
		}
	    }
	    return result;
	}

	bool LookBackInfo::equalsToArg2(std::vector<std::pair<std::string, long> > arg){
	    if (arg.size() != arg2_.size()){
		return false;
	    }
	    bool result = true;
	    for (size_t i = 0; i < arg.size(); i++){
		if (arg[i].first != arg2_[i].first){
		    result = false;
		    break;
		}
	    }
	    return result;
	}

	bool LookBackInfo::equalsToOp(std::string op){
	    if (op == op_.first){
		return true;
	    } else {
		return false;
	    }
	}

	std::vector<std::pair<std::string, long> > LookBackInfo::convertToArray(){
	    std::vector<std::pair<std::string, long> > result;
	    result.push_back(std::make_pair<std::string, long>("(", 1));
	    result.insert(result.end(), arg1_.begin(), arg1_.end());
	    result.push_back(op_);
	    result.insert(result.end(), arg2_.begin(), arg2_.end());
	    result.push_back(std::make_pair<std::string, long>(")", 1));
	    optimize_find_duplicate_string(result);
	    return result;
	}

	void LookBackInfo::optimize_find_duplicate_string(std::vector<std::pair<std::string, long> > &originalArray){
	    if (originalArray.size() < 20) return;

	    std::vector<std::vector<std::string> > levelInfo = generateLevelArray(originalArray);
	    if (levelInfo.size() == 0){
		return;
	    }

	    bool found = true;
	    while (found){
		found = false;
		for (size_t i = 1; i < levelInfo.size(); i++){
		    for (size_t j = 2; i + j * 2 < levelInfo.size(); j++){
			int counter = 0;
			int idx = 0;
			bool match = true;
			while (match){
			    if (i + j * (counter + 1) >= levelInfo.size()) break;
			    if (levelInfo[i + idx].size() == levelInfo[i + j * (counter + 1) + idx].size()){
				for (size_t k = 0; k < levelInfo[i + idx].size(); k++){
				    if (levelInfo[i + idx][k] != levelInfo[i + j * (counter + 1) + idx][k]){
					match = false;
					break;
				    }
				}  
			    } else {
				match = false;
			    }
			    if (match){
				if (idx == j - 1){
				    idx = 0;
				    counter += 1;
				} else {
				    idx += 1;
				}
			    }
			}

			if (counter >= 1){
			    levelInfo.erase(levelInfo.begin() + i, levelInfo.begin() + i + j* counter);
			    found = true;
			    break;
			}
		    }
		    if (found) break; 
		}
	    }

	    std::queue<std::string> levelQueue;
	    for (size_t i = levelInfo.size() - 1; i > 0; i--){
		for (size_t j = 0; j < levelInfo[i].size(); j++){
		    std::string curStr = levelInfo[i][j];
		    // handle sub_string and fromCharCode
		    size_t findSubString = curStr.find("substring");
		    size_t findFromCharCode = curStr.find("fromCharCode");
		    size_t findCharCodeAt = curStr.find("charCodeAt");
		    size_t findSplit = curStr.find("split");
		    size_t findReplace = curStr.find("replace");
		    size_t first = curStr.find("cv");
		    size_t last = curStr.rfind("cv");

		    if (findSubString != std::string::npos || findFromCharCode != std::string::npos ||
			findCharCodeAt != std::string::npos || findSplit != std::string::npos ||
			findReplace != std::string::npos){
			if (first == std::string::npos){
			    std::string pre = levelQueue.front();
			    levelQueue.pop();
			    curStr = "( " + pre + " " + curStr + " )";
			    levelQueue.push(curStr);
			}
			else{
			    curStr = "( " + curStr + " )";
			    levelQueue.push(curStr);
			}
			continue;
		    }

		    if (first == std::string::npos){
			std::string left = levelQueue.front();
			levelQueue.pop();
			std::string right = levelQueue.front();
			levelQueue.pop();
			curStr = "( " + left + " " + curStr + " " + right + " )";
			levelQueue.push(curStr);
		    }
		    else if (first == last){
			std::string pre = levelQueue.front();
			levelQueue.pop();
			if (first != 0){
			    curStr = "( " + pre + " " + curStr + " )";
			    levelQueue.push(curStr);
			}
			else{
			    curStr = "( " + curStr + " " + pre + " )";
			    levelQueue.push(curStr);
			}
		    }
		    else{
			curStr = "( " + curStr;
			curStr = curStr + " )";
			levelQueue.push(curStr);
		    }
		}
	    }

	    if (levelQueue.size() != 1){
		return; 
	    }

	    std::string newString = levelQueue.front();

	    std::vector<std::pair<std::string, long> > newArray;  
	    std::string tmp = "";
	    for (size_t i = 0; i < newString.size(); i++){
		if (newString[i] == ' '){
		    if (tmp.size() > 0){
			newArray.push_back(std::make_pair(tmp, 1));
		    }
		    tmp = "";
		    continue;
		}
		if (tmp == ""){
		    tmp = newString[i];
		} else {
		    tmp = tmp + newString[i];  
		}
	    }
	    if (tmp.size() > 0){
		newArray.push_back(std::make_pair(tmp, 1));
	    }
	    originalArray = newArray;
	}

	void LookBackInfo::print(std::string path){
	    if (path == ""){
		std::cout << "Original: " << std::endl;
		std::cout << "arg1:" << std::endl;;
		for (auto item : arg1_){
		    std::cout << item.first << ":" << item.second << std::endl;
		}

		std::cout << std::endl << "arg2:" << std::endl;;
		for (auto item : arg2_){
		    std::cout << item.first << ":" << item.second << std::endl;
		}

		std::vector<std::pair<std::string, long> > info = convertToArray();
		std::cout << std::endl << "convertToArray:" << std::endl;;
		for (auto item : info){
		    std::cout << item.first << ":" << item.second << std::endl;
		}
	    }
	    else{
		path = preparePathForSavingFile(path);
		
		std::vector<std::pair<std::string, long> > info = convertToArray();
		std::vector<std::vector<std::string> > levelInfo = generateLevelArray(info);
		std::ofstream logFile;
		logFile.open(path + "_jstrace");

		for (int i = 0; i < levelInfo.size(); i++){
		    for (int j = 0; j < levelInfo[i].size(); j++){
			logFile << levelInfo[i][j] << " | ";
		    }
		    logFile << "\n";
		}

		logFile.close();

		// also add string file used to calc similarity
		std::string stringpath = path + "_jstrace_string";
		std::ofstream stringFile;
		stringFile.open(stringpath);
		for (auto item : info){
		    stringFile << item.first;
		}
		stringFile.close();
	    }
	}

	void LookBackMap::printStatistic(std::string path){
	    std::streambuf *buf;
	    std::ofstream of;
	    std::string indicator = "";

	    if (path == ""){
		indicator += "[jstrace statistic] ";
		buf = std::cout.rdbuf();
	    }
	    else{
		path += "_jstrace_stat";
		path = preparePathForSavingFile(path);
		of.open(path.c_str());
		buf = of.rdbuf();
	    }

	    std::ostream out(buf);
	    
	    out << indicator << "Map size: " << stat_.size() << std::endl;
	    out << "----------------------------------------------" << std::endl;
	    for (std::unordered_map<std::string, long long>::iterator itr = stat_.begin(); itr != stat_.end(); itr++){
		out << indicator << itr->first << ": " << itr->second << std::endl;
	    }
	    out << "----------------------------------------------" << std::endl;

	    if (path != ""){
		of.close();
	    }
	}

	void LookBackMap::in_eval(){
	    in_eval_ = true;
	}

	void LookBackMap::not_in_eval(){
	    in_eval_ = false;
	}

	bool LookBackMap::contains(std::string key){
	    return (find(key) != nullptr);
	}

	void LookBackMap::print(std::string lookupHash, std::string objDesc, std::string path){
	    if (path == ""){
		LookBackInfo *info = find(lookupHash);
		if (info != nullptr){
		    info->print();
		}
	    }
	    else{
		LookBackInfo *info = find(lookupHash);
		if (info != nullptr){
		    char* domain = getenv("JSTRACE_DOMAIN");
		    char emptyDomain[] = "./";

		    if (domain == NULL){
			domain = emptyDomain;
		    }

		    std::string filePath = path + "/" + domain + "/";
		    if (v8::internal::JSTrace::getFileName().length() != 0){
			filePath += v8::internal::JSTrace::getFileName();
		    }
		    else{
			filePath += lookupHash;
		    }
		    
		    info->print(filePath);
		    v8::internal::JSTrace::saveSourceCode(filePath);
		    printStatistic(filePath);
		}
	    }  
	}

	LookBackInfo* LookBackMap::find(std::string lookupHash){
	    std::unordered_map<std::string, LookBackInfo>::iterator got = nbmap_.find(lookupHash);
	    if (got == nbmap_.end()){
		return nullptr;
	    }
	    else{
		return &(got->second);
	    }
	}

	void LookBackMap::init(){
	    //nbmap_.clear();
	    stat_.clear();
	    in_eval_ = false;
	}

	void LookBackMap::remove(std::string key){
	    std::unordered_map<std::string, LookBackInfo>::iterator got = nbmap_.find(key);
	    if (got != nbmap_.end()){
		nbmap_.erase(got);
	    }
	}

	void LookBackMap::assign(std::string target, std::string from){
	    LookBackInfo *pInfo = find(from);
	    LookBackInfo *pTargetInfo = find(target);
  
	    if (pInfo == nullptr){
		return;
	    }
  
	    LookBackInfo newInfo;
	    newInfo.op_ = pInfo->op_;
	    newInfo.arg1_ = pInfo->arg1_;
	    newInfo.arg2_ = pInfo->arg2_;

	    if (pTargetInfo != nullptr){
		remove(target);
	    }

	    nbmap_.insert({target, newInfo});

	    if (PRINT_DEBUG_INFO){
		std::cout << "[jstrace assign] from: " << from << " to " << target << std::endl;
	    }
	}

	void LookBackMap::append(std::string op, Handle<Object> hs, Handle<Object> result){
	    std::stringstream sstr, tstr;
	    Object *hsObj = *hs;
	    Object *resultObj = *result;
	    tstr << hsObj;
	    sstr << resultObj;
	    std::string arg = tstr.str();
	    std::string newObj = sstr.str();
	    
	    LookBackInfo *pArgInfo = find(arg);
	    LookBackInfo *pNewInfo = find(newObj);

	    LookBackInfo newInfo;
	    newInfo.arg2_.clear();
	    if (pArgInfo == nullptr){
		newInfo.op_ = std::make_pair(op, 1);
		newInfo.arg1_.push_back(std::make_pair<std::string, long>("cv", 1));
	    }
	    else{
		if (pArgInfo->equalsToOp(op)){
		    newInfo.op_ = pArgInfo->op_;
		    newInfo.op_.second += 1;
		    newInfo.arg1_ = pArgInfo->arg1_;
		}
		else{
		    newInfo.op_ = std::make_pair(op, 1);
		    newInfo.arg1_ = pArgInfo->convertToArray();
		    newInfo.arg2_.clear();
		}
	    }

	    if (pNewInfo != nullptr){
		remove(newObj);
	    }

	    statInsert(op);
	    
	    nbmap_.insert({newObj, newInfo});

	    if (PRINT_DEBUG_INFO){
		std::cout << "[jstrace append] " << op << ": " << newObj << ", arg: " << arg << std::endl;
	    }
	}

	void LookBackMap::append(std::string op, Handle<Object> lhs, Handle<Object> rhs, Handle<Object> result){
	    std::stringstream lstr, rstr, tstr;
	    Object *objAddr = *result;
	    Object *lAddr = *lhs;
	    Object *rAddr = *rhs;
	    lstr << lAddr;
	    rstr << rAddr;
	    tstr << objAddr;
	    std::string arg1 = lstr.str();
	    std::string arg2 = rstr.str();
	    std::string newObj = tstr.str();
	    
	    LookBackInfo *pArg1Info = find(arg1);
	    LookBackInfo *pArg2Info = find(arg2);
	    LookBackInfo *pNewInfo = find(newObj);

	    LookBackInfo newInfo;;
	    if (pArg1Info == nullptr && pArg2Info == nullptr){
		newInfo.op_ = std::make_pair(op, 1);
		newInfo.arg1_.push_back(std::make_pair<std::string, long>("cv", 1));
		newInfo.arg2_.push_back(std::make_pair<std::string, long>("cv", 1));
	    }
	    else if (pArg1Info == nullptr){
		std::vector<std::pair<std::string, long> > arg;
		arg.push_back(std::make_pair<std::string, long>("cv", 1));
		if (pArg2Info->equalsToOp(op) && pArg2Info->equalsToArg1(arg)){
		    newInfo.op_ = pArg2Info->op_;
		    newInfo.op_.second += 1;
		    newInfo.arg1_ = pArg2Info->arg1_;
		    newInfo.arg2_ = pArg2Info->arg2_;
		    newInfo.arg1_[0].second += 1;
		}
		else{
		    newInfo.op_ = std::make_pair(op, 1);
		    newInfo.arg1_.push_back(std::make_pair<std::string, long>("cv", 1));
		    newInfo.arg2_ = pArg2Info->convertToArray();
		}
	    }
	    else if (pArg2Info == nullptr){
		std::vector<std::pair<std::string, long> > arg;
		arg.push_back(std::make_pair<std::string, long>("cv", 1));
		if (pArg1Info->equalsToOp(op) && pArg1Info->equalsToArg2(arg)){
		    newInfo.op_ = pArg1Info->op_;
		    newInfo.op_.second += 1;
		    newInfo.arg1_ = pArg1Info->arg1_;
		    newInfo.arg2_ = pArg1Info->arg2_;
		    newInfo.arg2_[0].second += 1;
		}
		else{
		    newInfo.op_ = std::make_pair(op, 1);
		    newInfo.arg2_.push_back(std::make_pair<std::string, long>("cv", 1));
		    newInfo.arg1_ = pArg1Info->convertToArray();
		}
	    }
	    else{
		if (pArg1Info->equalsToOp(op) && pArg1Info->equalsToArg2(pArg2Info->convertToArray())){
		    newInfo.op_ = pArg1Info->op_;
		    newInfo.op_.second += 1;
		    newInfo.arg1_ = pArg1Info->arg1_;
		    newInfo.arg2_ = pArg1Info->arg2_;
		    std::vector<std::pair<std::string, long> > arg2 = pArg2Info->convertToArray();
		    for (size_t i = 0; i < arg2.size(); i++){
			newInfo.arg2_[i].second += arg2[i].second;
		    }
		}
		else if (pArg2Info->equalsToOp(op) && pArg2Info->equalsToArg1(pArg1Info->convertToArray())){
		    newInfo.op_ = pArg2Info->op_;
		    newInfo.op_.second += 1;
		    newInfo.arg1_ = pArg2Info->arg1_;
		    newInfo.arg2_ = pArg2Info->arg2_;
		    std::vector<std::pair<std::string, long> > arg1 = pArg1Info->convertToArray();
		    for (size_t i = 0; i < arg1.size(); i++){
			newInfo.arg1_[i].second += arg1[i].second;
		    }
		}
		else{
		    newInfo.op_ = std::make_pair(op, 1);
		    newInfo.arg1_ = pArg1Info->convertToArray();
		    newInfo.arg2_ = pArg2Info->convertToArray();
		}
	    }
  
	    if (pNewInfo != nullptr){
		remove(newObj);
	    }

	    statInsert(op);

	    nbmap_.insert({newObj, newInfo});

	    if (PRINT_DEBUG_INFO && !in_eval_){
		std::cout << "[jstrace append] ";

		if (result->IsString()){
		    Handle<String>::cast(result)->PrintOn(stdout);
		    std::cout << "(string, " << newObj << ") = ";
		}
		else if (result->IsNumber()){
		    std::cout << result->Number();
		    std::cout <<"(number, " << newObj << ") = ";
		}

		if (lhs->IsString()){
		    Handle<String>::cast(lhs)->PrintOn(stdout);
		    std::cout << "(string, " << arg1 << ") ";
		}
		else if (lhs->IsNumber()){
		    std::cout << lhs->Number();
		    std::cout <<"(number, " << arg1 << ") ";
		}

		std::cout << op << " ";

		if (rhs->IsString()){
		    Handle<String>::cast(rhs)->PrintOn(stdout);
		    std::cout << "(string, " << arg2 << ")";
		}
		else if (rhs->IsNumber()){
		    std::cout << rhs->Number();
		    std::cout <<"(number, " << arg2 << ")";
		}

		std::cout << std::endl;
	    }
	}
    }
}
