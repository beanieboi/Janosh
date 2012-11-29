#include "janosh.hpp"
#include "commands.hpp"

using std::string;
using std::map;
using std::vector;
using boost::make_iterator_range;
using boost::tokenizer;
using boost::char_separator;

namespace janosh {
  namespace kc = kyotocabinet;
  namespace js = json_spirit;
  namespace fs = boost::filesystem;

  using std::cerr;
  using std::cout;
  using std::endl;
  using std::string;
  using std::vector;
  using std::map;
  using std::istringstream;
  using std::ifstream;
  using std::exception;
  using boost::lexical_cast;

  TriggerBase::TriggerBase(const fs::path& config, const vector<fs::path>& targetDirs) :
    targetDirs(targetDirs) {
    load(config);
  }

  int TriggerBase::executeTarget(const string& name) {
    auto it = targets.find(name);
    if(it == targets.end()) {
      error("Target not found", name);
    }
    LOG_DEBUG_MSG("Execute target", name);
    return system((*it).second.c_str());
  }

  void TriggerBase::executeTrigger(const Path& p) {
    auto it = triggers.find(p);
    if(it != triggers.end()) {
      BOOST_FOREACH(const string& name, (*it).second) {
        executeTarget(name);
      }
    }
  }

  bool TriggerBase::findAbsoluteCommand(const string& cmd, string& abs) {
    bool found = false;
    string base;
    istringstream(cmd) >> base;

    BOOST_FOREACH(const fs::path& dir, targetDirs) {
      fs::path entryPath;
      fs::directory_iterator end_iter;

      for(fs::directory_iterator dir_iter(dir);
          !found && (dir_iter != end_iter); ++dir_iter) {

        entryPath = (*dir_iter).path();
        if (entryPath.filename().string() == base) {
          found = true;
          abs = dir.string() + cmd;
        }
      }
    }

    return found;
  }

  void TriggerBase::load(const fs::path& config) {
    if(!fs::exists(config)) {
      error("Trigger config doesn't exist", config);
      exit(1);
    }

    ifstream is(config.string().c_str());
    load(is);
    is.close();
  }

  void TriggerBase::load(std::ifstream& is) {
    try {
      js::Value triggerConf;
      js::read(is, triggerConf);

      BOOST_FOREACH(js::Pair& p, triggerConf.get_obj()) {
        string name = p.name_;
        js::Array arrCmdToTrigger = p.value_.get_array();

        if(arrCmdToTrigger.size() < 2){
          error("Illegal target", name);
        }

        string cmd = arrCmdToTrigger[0].get_str();
        js::Array arrTriggers = arrCmdToTrigger[1].get_array();
        string abs;

        if(!findAbsoluteCommand(cmd, abs)) {
          error("Target not found in search path", cmd);
        }

        targets[name] = abs;

        BOOST_FOREACH(const js::Value& v, arrTriggers) {
          triggers[v.get_str()].insert(name);
        }
      }
    } catch (exception& e) {
      error("Unable to load trigger configuration", e.what());
    }
  }


  Settings::Settings() {
    const char* home = getenv ("HOME");
    if (home==NULL) {
      error("Can't find environment variable.", "HOME");
    }
    string janoshDir = string(home) + "/.janosh/";
    fs::path dir(janoshDir);

    if (!fs::exists(dir)) {
      if (!fs::create_directory(dir)) {
        error("can't create directory", dir.string());
      }
    }

    this->janoshFile = fs::path(dir.string() + "janosh.json");
    this->triggerFile = fs::path(dir.string() + "triggers.json");
    this->logFile = fs::path(dir.string() + "janosh.log");

    if(!fs::exists(janoshFile)) {
      error("janosh configuration not found: ", janoshFile);
    } else {
      js::Value janoshConf;

      try{
        ifstream is(janoshFile.c_str());
        js::read(is,janoshConf);
        js::Object jObj = janoshConf.get_obj();
        js::Value v;

        if(find(jObj, "triggerDirectories", v)) {
          BOOST_FOREACH (const js::Value& vDir, v.get_array()) {
            triggerDirs.push_back(fs::path(vDir.get_str()));
          }
        }

        if(find(jObj, "database", v)) {
          this->databaseFile = fs::path(v.get_str());
        } else {

          error(this->janoshFile.string(), "No database file defined");
        }
      } catch (exception& e) {
        error("Unable to load jashon configuration", e.what());
      }
    }
  }

  bool Settings::find(const js::Object& obj, const string& name, js::Value& value) {
    auto it = find_if(obj.begin(), obj.end(),
        [&](const js::Pair& p){ return p.name_ == name;});
    if (it != obj.end()) {
      value = (*it).value_;
      return true;
    }
    return false;
  }


  Janosh::Janosh() :
    settings(),
    triggers(settings.triggerFile, settings.triggerDirs),
    channel_(),
    cm(makeCommandMap(this)) {
  }

  Janosh::~Janosh() {
  }

  void Janosh::setFormat(Format f) {
    this->format = f;
  }

  Format Janosh::getFormat() {
    return this->format;
  }

  void Janosh::open() {
    // open the database
    if (!Record::db.open(settings.databaseFile.string(), kc::PolyDB::OREADER | kc::PolyDB::OWRITER | kc::PolyDB::OCREATE)) {
      LOG_ERR_MSG("open error", Record::db.error().name());
      exit(2);
    }
  }

  bool Janosh::processRequest() {
    channel_.accept();
    bool success = true;
    vector<string> args;
    channel_.receive(args);

    std::vector<char*> vc;

    std::transform(args.begin(), args.end(), std::back_inserter(vc), [&](const std::string & s){
      char *pc = new char[s.size()+1];
      std::strcpy(pc, s.c_str());
      return pc;
    });

    int c;
    janosh::Format f = janosh::Bash;

    bool verbose = false;
    bool execTriggers = false;
    bool execTargets = false;

    string key;
    string value;
    string targetList;
    char* const* c_args = (char* const*)&vc[0];
    optind=1;
    while ((c = getopt(args.size(), c_args, "dvfjbrte:")) != -1) {
      switch (c) {
      case 'd':
        break;
      case 'f':
        if(string(optarg) == "bash")
          f=janosh::Bash;
        else if(string(optarg) == "json")
          f=janosh::Json;
        else if(string(optarg) == "raw")
          f=janosh::Raw;
         else
           LOG_ERR_MSG("Illegal format:", string(optarg));
           channel_.flush(false);
           return false;
        break;
      case 'j':
        f=janosh::Json;
        break;
      case 'b':
        f=janosh::Bash;
        break;
      case 'r':
        f=janosh::Raw;
        break;
      case 'v':
        verbose=true;
        break;
      case 't':
        execTriggers = true;
        break;
      case 'e':
        execTargets = true;
        targetList = optarg;
        break;
      case ':':
        LOG_ERR_MSG("Illegal option:", c);
        channel_.flush(false);
        return false;
        break;
      case '?':
        LOG_ERR_MSG("Illegal option:", c);
        channel_.flush(false);
        return false;
        break;
      }
    }

    this->setFormat(f);

    if(verbose)
      Logger::init(LogLevel::L_DEBUG);
    else
      Logger::init(LogLevel::L_INFO);

    vector<std::string> vecArgs;

    if(args.size() - optind >= 1) {
      string strCmd = string(args[optind].c_str());
      Command* cmd = this->cm[strCmd];
      if(!cmd) {
        LOG_ERR_MSG("Unknown command", strCmd);
        channel_.flush(false);
        return false;
      }

      vecArgs.clear();

      std::transform(
          args.begin() + optind + 1, args.end(),
          std::back_inserter(vecArgs),
          boost::bind(&std::string::c_str, _1)
          );

      Command::Result r = (*cmd)(vecArgs);
      LOG_INFO_MSG(r.second, r.first);
      success = r.first ? 1 : 0;
    } else if(!execTargets){
      channel_.flush(false);
      return false;
    }

    channel_.flush(success);

    if(execTriggers) {
      Command* t = cm["triggers"];
      boost::thread([=]() {
        vector<string> vecTriggers;

        for(size_t i = 0; i < vecArgs.size(); i+=2) {
          vecTriggers.push_back(vecArgs[i].c_str());
        }

        (*t)(vecTriggers);
      });
    }

    if(execTargets) {
      Command* t = cm["targets"];
      boost::thread([=]() {
      vector<string> vecTargets;
       tokenizer<char_separator<char> > tok(targetList, char_separator<char>(","));

       BOOST_FOREACH (const string& t, tok) {
         vecTargets.push_back(t);
       }

       (*t)(vecTargets);
      });
    }

    while(channel_.isOpen()) {
      boost::this_thread::sleep(boost::posix_time::milliseconds(10));
    };
    return success;
  }

  void Janosh::close() {
    Record::db.close();
  }

  size_t Janosh::loadJson(const string& jsonfile) {
    std::ifstream is(jsonfile.c_str());
    size_t cnt = this->loadJson(is);
    is.close();
    return cnt;
  }

  size_t Janosh::loadJson(std::istream& is) {
    js::Value rootValue;
    js::read(is, rootValue);

    Path path;
    return load(rootValue, path);
  }

  size_t Janosh::get(Record rec, std::ostream& out) {
    rec.fetch();
    size_t cnt = 1;

    LOG_DEBUG_MSG("print", rec.path());

    if(!rec.exists()) {
      JANOSH_ERROR("Path not found", rec.path());
    }

    if (rec.isDirectory()) {
      switch(this->getFormat()) {
        case Json:
          cnt = recurse(rec, JsonPrintVisitor(out));
          break;
        case Bash:
          cnt = recurse(rec, BashPrintVisitor(out));
          break;
        case Raw:
          cnt = recurse(rec, RawPrintVisitor(out));
          break;
      }
    } else {
      string value;

      switch(this->getFormat()) {
        case Raw:
        case Json:
          out << rec.value() << endl;
          break;
        case Bash:
          out << "\"( [" << rec.path() << "]='" << rec.value() << "' )\"" << endl;
          break;
      }
    }
    return cnt;
  }

  Record Janosh::makeTemp(const Value::Type& t) {
    Record tmp("/tmp/.");

    if(!tmp.fetch().exists()) {
      makeDirectory(tmp, Value::Array, 0);
      tmp.fetch();
    }

    if(t == Value::String) {
      tmp = tmp.path().withChild(tmp.getSize());
      this->add(tmp, "");
    } else {
      tmp = tmp.path().withChild(tmp.getSize()).asDirectory();
      this->makeDirectory(tmp, t, 0);
    }
    return tmp;
  }

  size_t Janosh::makeArray(Record target, size_t size, bool bounds) {
    JANOSH_TRACE({target}, size);
    target.fetch();
    Record base = Record(target.path().basePath());
    base.fetch();
    if(!target.isDirectory() || target.exists() || base.exists()) {
      JANOSH_ERROR("Invalid target",target.path());
    }

    if(bounds && !boundsCheck(target)) {
      JANOSH_ERROR("Out of array bounds",target.path());
    }
    changeContainerSize(target.parent(), 1);
    return Record::db.add(target.path(), "A" + lexical_cast<string>(size)) ? 1 : 0;
  }

  size_t Janosh::makeObject(Record target, size_t size) {
    JANOSH_TRACE({target}, size);
    target.fetch();
    Record base = Record(target.path().basePath());
    base.fetch();
    if(!target.isDirectory() || target.exists() || base.exists()) {
      JANOSH_ERROR("Invalid target",target.path());
    }

    if(!boundsCheck(target)) {
      JANOSH_ERROR("Out of array bounds", target.path());
    }

    if(!target.path().isRoot())
      changeContainerSize(target.parent(), 1);
    return Record::db.add(target.path(), "O" + lexical_cast<string>(size)) ? 1 : 0;
  }

  size_t Janosh::makeDirectory(Record target, Value::Type type, size_t size) {
    JANOSH_TRACE({target}, size);
    if(type == Value::Array) {
      return makeArray(target, size);
    } else if (type == Value::Object) {
      return makeObject(target, size);
    } else
      assert(false);
    return 0;
  }

  /**
   * Adds a record with the given value to the database.
   * Does not modify an existing record.
   * @param dest destination record
   * @return TRUE if the destination record didn't exist.
   */
  size_t Janosh::add(Record dest, const string& value) {
    JANOSH_TRACE({dest}, value);

    if(!dest.isValue() || dest.exists()) {
      JANOSH_ERROR("Invalid target", dest.path());
    }

    if(!boundsCheck(dest)) {
      JANOSH_ERROR("Out of array bounds",dest.path());
    }

    if(Record::db.add(dest.path(), value)) {
//      if(!dest.path().isRoot())
        changeContainerSize(dest.parent(), 1);
      return 1;
    } else {
      return 0;
    }
  }

  /**
   * Relaces a the value of a record.
   * If the destination record exists create it.
   * @param dest destination record
   * @return TRUE if the record existed.
   */
  size_t Janosh::replace(Record dest, const string& value) {
    JANOSH_TRACE({dest}, value);
    dest.fetch();

    if(!dest.isValue() || !dest.exists()) {
      JANOSH_ERROR("Invalid target", dest.path());
    }

    if(!boundsCheck(dest)) {
      JANOSH_ERROR("Out of array bounds", dest.path());
    }

    return Record::db.replace(dest.path(), value);
  }


  /**
   * Relaces a destination record with a source record to the new path.
   * If the destination record exists replaces it.
   * @param src source record. Points to the destination record after moving.
   * @param dest destination record.
   * @return TRUE on success
   */
  size_t Janosh::replace(Record& src, Record& dest) {
    JANOSH_TRACE({src, dest});
    src.fetch();
    dest.fetch();

    if(src.isRange() || !src.exists() || !dest.exists()) {
      JANOSH_ERROR("Invalid target", dest.path());
    }

    if(!boundsCheck(dest)) {
      JANOSH_ERROR("Out of array bounds", dest.path());
    }

    Record target;
    size_t r;
    if(dest.isDirectory()) {
      if(src.isDirectory()) {
        target = dest.path();
        Record wildcard = src.path().asWildcard();
        r = this->copy(wildcard,target);
      } else {
        target = dest.path().basePath();
        remove(dest,false);
        r = this->copy(src,target);
      }

      dest = target;
    } else {
      if(src.isDirectory()) {
        Record target = dest.path().asDirectory();
        Record wildcard = src.path().asWildcard();
        remove(dest,false);
        makeDirectory(target, src.getType());
        r = this->append(wildcard, target);
        dest = target;

      } else {
        r = Record::db.replace(dest.path(), src.value());
      }
    }

    src.fetch();
    dest.fetch();

    return r;
  }

  /**
   * Copies source record value to the destination record - eventually removing the source record.
   * If the destination record exists replaces it.
   * @param src source record. Points to the destination record after moving.
   * @param dest destination record.
   * @return TRUE on success
   */
  size_t Janosh::move(Record& src, Record& dest) {
    JANOSH_TRACE({src, dest});
    src.fetch();
    dest.fetch();

    if(src.isRange() || !src.exists() || !dest.exists()) {
      JANOSH_ERROR("Invalid src", src.path());
    }

    if(!boundsCheck(dest)) {
      JANOSH_ERROR("Out of array bounds", dest.path());
    }

    Record target;
    size_t r;
    if(dest.isDirectory()) {
      if(src.isDirectory())
        target = dest;
      else
        target = dest.path().basePath();

      remove(dest);

      r = this->copy(src,target);
      dest = target;
    } else {
      if(src.isDirectory()) {
        target = dest.parent();
        r = this->copy(src, target);
        dest = target;
      } else {
        r = Record::db.replace(dest.path(), src.value());
      }
    }


    src = dest;

    return r;
  }


  /**
   * Sets/replaces the value of a record. If no record exists, creates the record with corresponding value.
   * @param rec The record to manipulate
   * @return TRUE on success
   */
  size_t Janosh::set(Record rec, const string& value) {
    JANOSH_TRACE({rec}, value);

    if(!rec.isValue()) {
      JANOSH_ERROR("Invalid target", rec.path());
    }

    if(!boundsCheck(rec)) {
      JANOSH_ERROR("Out of array bounds", rec.path());
    }

    rec.fetch();
    if (rec.exists())
      return replace(rec, value);
    else
      return add(rec, value);
  }


  /**
   * Removes a record from the database.
   * @param rec The record to remove. Points to the next record in the database after removal.
   * @return number of total records affected.
   */
  size_t Janosh::remove(Record& rec, bool pack) {
    JANOSH_TRACE({rec});
    if(!rec.isInitialized())
      return 0;
 
    size_t n = rec.fetch().getSize();
    size_t cnt = 0;

    Record parent = rec.parent();

    Path targetPath = rec.path();
    Record target(targetPath);

    if(rec.isDirectory() || rec.isRange()) {
      rec.step();

      for(size_t i = 0; i < n; ++i) {
        if(!rec.isDirectory()) {
          rec.remove();
          ++cnt;
        } else {
          cnt += remove(rec);
        }
      }
    }

    if(!targetPath.isWildcard()) {
      if(pack && parent.fetch().isArray()) {
        size_t parentSize = parent.getSize();
        size_t index = targetPath.parseIndex();

        if(index + 1 < parentSize) {
          Record forwardRec = targetPath;
          Record backRec = targetPath;
          forwardRec.fetch().next();

          for(size_t i=0; i < parentSize - 1 - index; ++i) {
            replace(forwardRec, backRec);
            backRec.next();
            forwardRec.next();
          }
          if(forwardRec.fetch().isDirectory())
            cnt += remove(forwardRec);
          else {
            forwardRec = forwardRec.path();
            cnt += forwardRec.fetch().remove();
          }
        } else {
          cnt += target.fetch().remove();
        }
      } else {
        cnt += target.fetch().remove();
      }
      changeContainerSize(parent, -1);
    } else {
      parent.fetch();
      setContainerSize(parent, 0);
    }

    rec = target;

    return cnt;
  }

  size_t Janosh::dump() {
    kc::DB::Cursor* cur = Record::db.cursor();
    string key,value;
    cur->jump();
    size_t cnt = 0;

    while(cur->get(&key, &value, true)) {
      this->channel_.out() << "path: " << Path(key).pretty() <<  " value:" << value << endl;
      ++cnt;
    }
    delete cur;
    return cnt;
  }

  size_t Janosh::hash() {
    kc::DB::Cursor* cur = Record::db.cursor();
    string key,value;
    cur->jump();
    size_t cnt = 0;
    boost::hash<string> hasher;
    size_t h = 0;
    while(cur->get(&key, &value, true)) {
      h = hasher(lexical_cast<string>(h) + key + value);
      ++cnt;
    }
    this->channel_.out() << h << std::endl;
    delete cur;
    return cnt;
  }

  size_t Janosh::truncate() {
    if(Record::db.clear())
      return Record::db.add("/!", "O" + lexical_cast<string>(0)) ? 1 : 0;
    else
      return false;
  }

  size_t Janosh::size(Record rec) {
    if(!rec.isDirectory()) {
      JANOSH_ERROR("size is limited to containers", rec.path());
    }

    return rec.fetch().getSize();
  }

  size_t Janosh::append(Record dest, const string& value) {
    JANOSH_TRACE({dest}, value);
    if(!dest.isDirectory()) {
      JANOSH_ERROR("append is limited to dest directories", dest.path());
    }

    Record target(dest.path().withChild(dest.getSize()));
    if(!Record::db.add(target.path(), value)) {
      JANOSH_ERROR("Failed to add target", target.path());
    }

    dest = target;
    return 1;
  }

  size_t Janosh::append(vector<string>::const_iterator begin, vector<string>::const_iterator end, Record dest) {
    JANOSH_TRACE({dest});
    if(!dest.isDirectory()) {
      JANOSH_ERROR("append is limited to dest directories", dest.path());
    }
    dest.fetch();
    size_t s = dest.getSize();
    size_t cnt = 0;

    for(; begin != end; ++begin) {
      if(!Record::db.add(dest.path().withChild(s + cnt), *begin)) {
        JANOSH_ERROR("Failed to add target", dest.path());
      }
      ++cnt;
    }

    setContainerSize(dest, s + cnt);
    return cnt;
  }

  size_t Janosh::append(Record& src, Record& dest) {
    JANOSH_TRACE({src,dest});

    if(!dest.isDirectory()) {
      JANOSH_ERROR("append is limited to directories", dest.path());
    }

    src.fetch();
    dest.fetch();

    size_t n = src.getSize();
    size_t s = dest.getSize();
    size_t cnt = 0;

    string path;
    string value;

    do {
      src.read();
      if(src.isAncestorOf(dest)) {
        JANOSH_ERROR("can't append an ancestor", src.path());
      }

      if(src.isDirectory()) {
        Record target;
        if(dest.isObject()) {
          target = dest.path().withChild(src.path().name()).asDirectory();
        } else if(dest.isArray()) {
          target = dest.path().withChild(s + cnt).asDirectory();
        } else
          assert(false);

        if(src.isAncestorOf(target)) {
          JANOSH_ERROR("can't append an ancestor",src.path());
        }


        if(!makeDirectory(target, src.getType())) {
          JANOSH_ERROR("failed to create directory", target.path());
        }

        Record wildcard = src.path().asWildcard();
        if(!append(wildcard, target)) {
          JANOSH_ERROR("failed to append values", target.path());
        }
      } else {
        if(dest.isArray()) {
          Path target = dest.path().withChild(s + cnt);
          if(!Record::db.add(
              target,
              src.value()
          )) {
            JANOSH_ERROR("failed to add", target);
          }
        } else if(dest.isObject()) {
          Path target = dest.path().withChild(src.path().name());
          if(!Record::db.add(
              target,
              src.value()
          )) {
            JANOSH_ERROR("failed to add",target);
          }
        }
      }
    } while(++cnt < n && src.next());

    setContainerSize(dest, s + cnt);
    return cnt;
  }

  size_t Janosh::copy(Record& src, Record& dest) {
    JANOSH_TRACE({src,dest});

    src.fetch();
    dest.fetch();
    if(dest.exists() && !src.isRange()) {
      JANOSH_ERROR("Destination already exists", dest.path());
    }

    if(dest.isRange()) {
      JANOSH_ERROR("Destination can't be a range", dest.path());
    }

    if(src == dest)
      return 0;

    if(!src.isValue() && !dest.isDirectory()) {
      JANOSH_ERROR("invalid target", dest.path());
    }

    if((src.isRange() || src.isDirectory()) && !dest.exists()) {
      makeDirectory(dest, src.getType());
      Record wildcard = src.path().asWildcard();
      return this->append(wildcard, dest);
    } else if (src.isValue() && dest.isValue()) {
      return this->set(dest, src.value());
    } else {
      return this->append(src, dest);
    }

    return 0;
  }

  size_t Janosh::shift(Record& src, Record& dest) {
    JANOSH_TRACE({src,dest});

    Record srcParent = src.parent();
    srcParent.fetch();

    if(!srcParent.isArray() || srcParent != dest.parent()) {
      JANOSH_ERROR("Move is limited within one array", src.path().key() + "->" + dest.path().key());
    }

    size_t parentSize = srcParent.getSize();
    size_t srcIndex = src.getIndex();
    size_t destIndex = dest.getIndex();

    if(srcIndex >= parentSize || destIndex >= parentSize) {
      JANOSH_ERROR("index out of bounds", src.path());
    }

    bool back = srcIndex > destIndex;
    Record forwardRec = src.clone().fetch();
    Record backRec = src.clone().fetch();

    if(back) {
      assert(forwardRec.previous());
    } else {
      assert(forwardRec.next());
    }

    Record tmp;

    if(src.isDirectory()) {
      tmp = makeTemp(src.getType());
      Record wildcard = src.path().asWildcard();
      this->append(wildcard, tmp);
    } else {
      tmp = makeTemp(Value::String);
      this->set(tmp, src.value());
    }

    for(size_t i=0; i < abs(destIndex - srcIndex); ++i) {
      replace(forwardRec, backRec);
      if(back) {
        backRec.previous();
        forwardRec.previous();
      } else {
        backRec.next();
        forwardRec.next();
      }
    }

    tmp.fetch();
    replace(tmp,dest);
    remove(tmp);

    src = dest;

    return 1;
  }

  void Janosh::setContainerSize(Record container, const size_t s) {
    JANOSH_TRACE({container}, s);

    string containerValue;
    string strContainer;
    const Value::Type& containerType = container.getType();
    char t;

    if (containerType == Value::Array)
     t = 'A';
    else if (containerType == Value::Object)
     t = 'O';
    else
     assert(false);

    const string& new_value =
       (boost::format("%c%d") % t % (s)).str();

    container.setValue(new_value);
  }

  void Janosh::changeContainerSize(Record container, const size_t by) {
    container.fetch();
    setContainerSize(container, container.getSize() + by);
  }

  size_t Janosh::load(const Path& path, const string& value) {
    LOG_DEBUG_MSG("add", path.key());
    return Record::db.add(path, value) ? 1 : 0;
  }

  size_t Janosh::load(js::Value& v, Path& path) {
    size_t cnt = 0;
    if (v.type() == js::obj_type) {
      cnt+=load(v.get_obj(), path);
    } else if (v.type() == js::array_type) {
      cnt+=load(v.get_array(), path);
    } else {
      cnt+=this->load(path, v.get_str());
    }
    return cnt;
  }

  size_t Janosh::load(js::Object& obj, Path& path) {
    size_t cnt = 0;
    path.pushMember(".");
    cnt+=this->load(path, (boost::format("O%d") % obj.size()).str());
    path.pop();

    BOOST_FOREACH(js::Pair& p, obj) {
      path.pushMember(p.name_);
      cnt+=load(p.value_, path);
      path.pop();
      ++cnt;
    }

    return cnt;
  }

  size_t Janosh::load(js::Array& array, Path& path) {
    size_t cnt = 0;
    int index = 0;
    path.pushMember(".");
    cnt+=this->load(path, (boost::format("A%d") % array.size()).str());
    path.pop();

    BOOST_FOREACH(js::Value& v, array){
      path.pushIndex(index++);
      cnt+=load(v, path);
      path.pop();
      ++cnt;
    }
    return cnt;
  }

  bool Janosh::boundsCheck(Record p) {
    Record parent = p.parent();

    p.fetch();
    parent.fetch();

    return (parent.path().isRoot() || (!parent.isArray() || p.path().parseIndex() <= parent.getSize()));
  }
}

void printUsage() {
  std::cerr << "janosh [-l] <path>" << endl
      << endl
      << "<path>         the json path (uses / as separator)" << endl
      << endl
      << "Options:" << endl
      << endl
      << "-l             load a json snippet from standard input" << endl
      << endl;
  exit(1);
}

std::vector<size_t> sequence() {
  std::vector<size_t> v(10);
  size_t off = 5;
  std::generate(v.begin(), v.end(), [&]() {
    return ++off;
  });
  return v;
}

kyotocabinet::TreeDB janosh::Record::db;

int main(int argc, char** argv) {
  using namespace std;
  using namespace janosh;
  bool daemon = false;
  int c;

  while ((c = getopt(argc, argv, "dvfjbrte:")) != -1) {
    switch (c) {
    case 'd':
      daemon = true;
      break;
    default:
      break;
    }
  }

  if(daemon) {
    Janosh janosh;
    janosh.open();
    for(;;) {
      std:: cerr << "result: " << janosh.processRequest() << std::endl;
    };
    janosh.close();
  } else {
    //client passes the argv to the daemon
    Channel chan;
    chan.connect();
    for(int i = 0; i < argc; i++) {
      chan.writeln(argv[i]);
    }
    chan.flush(true);
    bool success = chan.receive(std::cout);
    chan.close();
    return success;
  }

  return 0;
}
   


