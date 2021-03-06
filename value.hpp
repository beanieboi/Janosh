#ifndef VALUE_HPP_
#define VALUE_HPP_

#include <string>
#include "path.hpp"
#include "exception.hpp"

namespace janosh {
  using std::string;

  class Value {
  public:
    enum Type {
      Null, String, Array, Object, Range
    };

    Value();
    Value(Type t);
    Value(const string& v, Type t);
    Value(const string v, bool dir);
    Value(const Value& other);
    bool isInitialized() const;
    bool isEmpty() const;
    void reset();
    const string& str() const;

    operator string() const {
      return this->str();
    }

    operator string() {
      return this->str();
    }
    const Type getType()  const;
    const size_t getSize() const;
    void init(const string& v, bool value);

  private:
    string strObj;
    Type type;
    size_t size;
    bool initalized;
  };


  typedef boost::error_info<struct tag_janosh_path,std::pair<std::string,std::string> > value_info;
  struct value_exception : virtual janosh_exception { };
}
#endif /* VALUE_HPP_ */
