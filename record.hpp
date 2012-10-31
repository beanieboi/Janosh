#ifndef _JANOSH_DBPATH_HPP
#define _JANOSH_DBPATH_HPP

#include <stack>
#include <sstream>
#include <iostream>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>
#include <boost/smart_ptr.hpp>
#include <kcpolydb.h>
#include "Logger.hpp"
#include <bitset>

namespace janosh {
  namespace kc = kyotocabinet;
  typedef kc::DB::Cursor Cursor;

  struct Component {
      string _key;
      string _pretty;

      const string keyEncodeIndex(const size_t& n) const {
        using namespace std;
        return bitset<std::numeric_limits<size_t>::digits>(n).to_string<char, char_traits<char>, allocator<char> >();
      }

      const size_t keyDecodeIndex(const string& s) const {
        return std::bitset<std::numeric_limits<size_t>::digits>(s).to_ulong();
      }

    public:
      Component() {}

      Component(const string& c) {
        if(!c.empty()) {
          if(c.at(0) == '#') {
            size_t i = boost::lexical_cast<size_t>(c.substr(1));
            this->_key = "$" + keyEncodeIndex(i);
            this->_pretty = "#" + boost::lexical_cast<string>(i);
          } else if(c.at(0) == '$') {
            const string enc = c.substr(1);
            size_t dec = keyDecodeIndex(enc);
            this->_key = "$" + enc;
            this->_pretty = "#" + boost::lexical_cast<string>(dec);
          } else if(c.at(0) == '.'){
            this->_key = "!";
            this->_pretty = c;
          } else if(c.at(0) == '!'){
            this->_key = "!";
            this->_pretty = ".";
          } else {
            this->_key = c;
            this->_pretty = this->_key;
          }
        }
      }

      bool operator==(const Component& other) const {
        return this->key() == other.key();
      }

      bool operator!=(const Component& other) const {
        return !(*this == other);
      }

      bool operator<(const Component& other) const {
        return this->key() <  other.key();
      }

      bool isIndex() {
        return _pretty.at(0) == '#';
      }

      bool isValid() {
        return !_pretty.empty() && !_key.empty();
      }

      bool isDirectory() {
        return _pretty == ".";
      }

      bool isWildcard() {
        return _pretty == "*";
      }

      const string& key() const {
        return this->_key;
      }

      const string& pretty() const {
        return this->_pretty;
      }

    /*  operator string() const {
        return this->key();
      }*/
    };

  class Path {
    string keyStr;
    string prettyStr;
    std::vector<Component> components;
    bool directory;
    bool wildcard;

    string compilePathString() const {
      std::stringstream ss;

      for (auto it = this->components.begin(); it != this->components.end(); ++it) {
        ss << '/' << (*it).key();
      }
      return ss.str();
    }

public:
    Path() :
      directory(false),
      wildcard(false)
    {}

    Path(const char* path) :
        directory(false),
        wildcard(false) {
      update(path);
    }

    Path(const string& strPath) :
        directory(false),
        wildcard(false) {
      update(strPath);
    }

    Path(const Path& other) {
      this->keyStr = other.keyStr;
      this->prettyStr = other.prettyStr;
      this->directory = other.directory;
      this->wildcard = other.wildcard;
      this->components = other.components;
    }

    void update(const string& p) {
      using namespace boost;
      if(p.empty()) {
        this->reset();
        return;
      }

      if(p.at(0) != '/') {
        LOG_ERR_MSG("Illegal Path", p);
        exit(1);
      }

      char_separator<char> ssep("[/", "", boost::keep_empty_tokens);
      tokenizer<char_separator<char> > tokComponents(p, ssep);
      this->components.clear();
      tokComponents.begin();
      if(tokComponents.begin() != tokComponents.end()) {
        auto it = tokComponents.begin();
        it++;
        for(; it != tokComponents.end(); it++) {
          const string& c = *it;
          if(c.empty()) {
            LOG_ERR_MSG("Illegal Path", p);
            exit(1);
          }
          this->components.push_back(Component(c));
        }

        this->keyStr.clear();
        this->prettyStr.clear();
        for(auto it = this->components.begin(); it != this->components.end(); ++it) {
          this->keyStr+= "/" + (*it).key();
          this->prettyStr+= "/" + (*it).pretty();
        }

        this->directory = !this->components.empty() && this->components.back().isDirectory();
        this->wildcard = !this->components.empty() && this->components.back().isWildcard();
      } else {
        reset();
      }
    }

    operator string() const {
      return this->key();
    }

    bool operator<(const Path& other) const {
      return this->key() < other.key();
    }

    bool operator==(const string& other) const {
      return this->keyStr == other;
    }

    bool operator==(const Path& other) const {
      return this->keyStr == other.keyStr;
    }

    const string key() const {
      return this->keyStr;
    }

    const string pretty() const {
      return this->prettyStr;
    }

    const bool isWildcard() const {
      return this->wildcard;
    }

    const bool isDirectory() const {
      return this->directory;
    }

    bool isEmpty() const {
      return this->keyStr.empty();
    }

    bool isRoot() const {
      return this->prettyStr == "/.";
    }

    Path asDirectory() const {
      return Path(this->basePath().key() + "/!");
    }

    Path asWildcard() const {
      return Path(this->basePath().key() + "/*");
    }

    Path withChild(const Component& c) const{
      return Path(this->basePath().key() + '/' + c.key());
    }

    Path withChild(const size_t& i) const {
      return Path(this->basePath().key() + "/#" + boost::lexical_cast<string>(i));
    }

    void pushMember(const string& name) {
      components.push_back((boost::format("%s") % name).str());
      update(compilePathString());
    }

    void pushIndex(const size_t& index) {
      components.push_back((boost::format("#%d") % index).str());
      update(compilePathString());
    }

    void pop(bool doUpdate=false) {
      components.erase(components.end() - 1);
      if(doUpdate) update(compilePathString());
    }

    Path basePath() const {
      if(isDirectory() || isWildcard()) {
        return Path(this->keyStr.substr(0, this->keyStr.size() - 2));
      } else {
        return Path(*this);
      }
    }

    Component name() const {
      size_t d = 1;

      if(isDirectory())
        ++d;

      if(components.size() >= d)
        return (*(components.end() - d));
      else
        return Component();
    }

    size_t parseIndex() const {
        return boost::lexical_cast<size_t>(this->name().pretty().substr(1));
    }

    Path parent() const {
      Path parent(this->basePath());
      if(!parent.isEmpty() && !this->isWildcard())
        parent.pop(false);
      parent.pushMember(".");

      return parent;
    }

    Component parentName() const {
      size_t d = 2;
      if(isDirectory())
        ++d;

      if(components.size() >= d)
        return (*(components.end() - d)).key();
      else
        return Component();
    }

    string root() const {
      return components.front().key();
    }

    void reset() {
      this->keyStr.clear();
      this->prettyStr.clear();
      this->components.clear();
      this->directory = false;
      this->wildcard = false;
    }

    const bool above(const Path& other) const {
      if(other.components.size() >= this->components.size() ) {
        size_t depth = this->components.size();
        if(this->isDirectory()) {
          depth--;
        }

        size_t i;
        for(i = 0; i < depth; ++i) {

          const string& tc = this->components[i].key();
          const string& oc = other.components[i].key();

          if(oc != tc) {
            return false;
          }
        }

        return true;
      }

      return false;
    }
  };

  class Value {
  public:
    enum Type {
      Null,
      String,
      Array,
      Object,
      Range
    };

    Value() : strObj(), type(Null), initalized(false) {}

    Value(Type t) :
      initalized(true) {
      init("", t == String);
    }

    Value(const string& v, Type t) :
      initalized(true) {
      init(v, t == String);
    }

    Value(const string v, bool dir) :
      initalized(true) {
      init(v, !dir);
    }

    Value(const Value& other) {
      this->strObj = other.strObj;
      this->type = other.type;
      this->size = other.size;
      this->initalized = other.initalized;
    }

    bool isInitialized() const {
      return initalized;
    }

    bool isEmpty() const {
      return strObj.empty();
    }

    void reset() {
      this->strObj.clear();
      this->initalized = false;
    }

    const string& str() const {
      assert(initalized);
      return this->strObj;
    }

    operator string() const {
      assert(initalized);
      return this->strObj;
    }

    operator string() {
      assert(initalized);
      return this->strObj;
    }

    const Type getType()  const {
      assert(isInitialized());
      assert(!isEmpty());

      return this->type;
    }

    const size_t getSize() const {
      assert(isInitialized());
      assert(!isEmpty());

      return this->size;
    }

  private:
    void init(const string& v, bool value) {
      if(!value) {
        char c = v.at(0);
        if(c == 'A') {
          this->type = Array;
        } else if(c == 'O') {
          this->type = Object;
        } else {
          assert(!"Unknown directory descriptor");
        }

        this->size = boost::lexical_cast<size_t>(v.substr(1));
      } else {
        this->type = Type::String;
        this->size = 1;
      }

      this->strObj = v;
    }
    string strObj;
    Type type;
    size_t size;
    bool initalized;
  };

  class Record : private boost::shared_ptr<kc::DB::Cursor> {
    Path pathObj;
    Value valueObj;
    bool doesExist;

    void init(const Path path);
  public:
    static kc::TreeDB db;

    //exact copy referring to the same Cursor*
    Record(const Record& other);
    Record clone();
    Record(const Path& path);
    Record();


    kc::DB::Cursor* getCursorPtr();
    const Value::Type getType()  const;
    const size_t getSize() const;
    const size_t getIndex() const;

    bool get(string& k, string& v);
    bool setValue(const string& v);

    void clear();

    bool jump(const Path& p);
    bool jump_back(const Path& p);
    bool step();
    bool step_back();
    bool next();
    bool previous();
    bool remove();

    const Path& path() const;
    const Value& value() const;
    Record parent() const;


    const bool isAncestorOf(const Record& other) const;
    const bool isArray() const;
    const bool isDirectory() const;
    const bool isRange() const;
    const bool isValue() const;
    const bool isObject() const;
    const bool isInitialized() const;
    const bool hasData() const;
    const bool exists() const;
    const bool empty() const;

    Record& fetch();
    bool readValue();
    bool readPath();
    bool read();


    bool operator==(const Record& other) const;
    bool operator!=(const Record& other) const;
  };

  std::ostream& operator<< (std::ostream& os, const janosh::Path& p);
  std::ostream& operator<< (std::ostream& os, const janosh::Value& v);
}



#endif
