/*
Copyright (c) Visillect Service LLC. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of copyright holders.
*/


#pragma once

#include <cassert>
#include <boost/noncopyable.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cstdio>
#include <vector>
#include <map>
#include <utility>
#include <mxprops/pathprop.h>

namespace mxprops {

class PropsError : public std::runtime_error
{
public:
  PropsError(std::string const& propertyName, std::string const& msg)
  : std::runtime_error(msg + propertyName),
    propertyName(propertyName)
  { }

  ~PropsError() throw() { }

  char const* getPropertyName() const { return propertyName.c_str(); }

private:
  std::string propertyName;
};

class PTree : private boost::noncopyable
{
public:
  class Record
  {
  public:
    Record()
    : defined(false)
    { }

    std::string const& getValue() const { return value; }

    void setValue(std::string const& v)
    {
      value = v;
      defined = true;
    }

    void setValue(std::string const& v, PathPropData const& pd)
    {
      setValue(v);
      pathData = pd;
    }

    bool isDefined() const { return defined; }

    template <typename TData>
    boost::optional<TData> get_as(bool *getDefined = 0) const
    {
      if (getDefined)
        *getDefined = defined;

      if (!defined)
        return boost::optional<TData>();
      typedef typename boost::property_tree::translator_between<std::string, TData>::type Tr;
      return Tr().get_value(value);
    }

    template <typename TData>
    void set_as(TData const& d)
    {
      typedef typename boost::property_tree::translator_between<std::string, TData>::type Tr;
      boost::optional<std::string> strTranslated = Tr().put_value(d);
      defined = strTranslated;
      value = strTranslated.get_value_or("<invalid>");
    }

    void undefine()
    {
      defined = false;
    }

    PathPropData const& getPathData() const
    {
      return pathData;
    }

  private:
    std::string value;
    bool defined;
    PathPropData pathData;
  };

public:
  PTree()
  { }

  class Ref;
  class ConstRef;

  ConstRef root(const std::string &id) const;
  Ref      root(const std::string &id);

  static std::string joinPaths(const std::string &p1, const std::string &p2)
  {
    if (p1.empty())
      return p2;
    if (p2.empty())
      return p1;
    return p1 + "." + p2;
  }

  static bool isSimplePath(const std::string &p)
  {
    return !p.empty() && p.find('.') == std::string::npos;
  }

  static std::string splitFirst(const std::string &p)
  {
    size_t const pos = p.find('.');
    if (pos == std::string::npos)
      return p;
    else
      return p.substr(0, pos);
  }

  void clear()
  {
    propMap.clear();
  }

private:

  typedef std::map<std::string, Record> propmap_t;
  propmap_t propMap;

  Record & getRecord(std::string const& path)
  {
    return propMap[path];
  }

  friend class Ref;
  friend class ConstRef;

  boost::mutex mutex;
};

class PTree::ConstRef
{
public:
  ConstRef()
  : owner(0)
  { }

  bool hasOwner() const { return owner != 0; }

  std::string getSelfPath() const { return selfPath; }

  PTree::Record getRecord(const std::string &path) const
  {
    assert(owner);
    return owner->getRecord(PTree::joinPaths(selfPath, path));
  }

  template <typename TData>
  TData get(const std::string &path, const TData &defaultValue) const
  {
    using boost::lexical_cast;
    assert(owner);
    boost::unique_lock<boost::mutex> g(owner->mutex);
    return getRecord(path).get_as<TData>().get_value_or(defaultValue);
  }

  template <typename TData>
  boost::optional<TData> getOptional(const std::string &path, bool *getDefined = 0) const
  {
    assert(owner);
    boost::unique_lock<boost::mutex> g(owner->mutex);
    return getRecord(path).get_as<TData>(getDefined);
  }

  template <typename TData>
  TData get(const std::string &path) const
  {
    bool isDefined = false;
    boost::optional<TData> const v = getOptional<TData>(path, &isDefined);
    if (!v)
      throw PropsError(getSelfPath() + "." + path, isDefined ? "Bad format " : "Undefined property: ");
    return *v;
  }

  template <typename TData>
  TData getValue(const TData &defaultValue) const
  {
    return get<TData>("", defaultValue);
  }

  template <typename TData>
  boost::optional<TData> getValue() const
  {
    return getOptional<TData>("");
  }

  void listKeysRecursive(std::vector<std::string> & result, bool withUndefined = false) const
  {
    assert(owner);
    boost::unique_lock<boost::mutex> g(owner->mutex);

    propmap_t const& pm = owner->propMap;
    propmap_t::const_iterator it = pm.lower_bound(selfPath);
    if (selfPath.empty())
    {
      while (it != pm.end())
      {
        if (withUndefined || it->second.isDefined())
          result.push_back(it->first);
        ++it;
      }
    }
    else
    {
      while (it != pm.end() && boost::starts_with(it->first, selfPath))
      {
        if (withUndefined || it->second.isDefined())
        {
          if (selfPath == it->first)
            result.push_back("");
          else
            result.push_back(it->first.substr(selfPath.size() + 1)); // to remove the path separator
        }
        ++it;
      }
    }
  }

  void listKeys(std::vector<std::string> & result, bool withUndefined = false) const
  {
    std::vector<std::string> allKeys;
    listKeysRecursive(allKeys, withUndefined);
    std::string prevKey;
    for (size_t i = 0; i < allKeys.size(); ++i)
    {
      std::string const k = splitFirst(allKeys[i]);
      if (k != prevKey)
      {
        result.push_back(k);
        prevKey = k;
      }
    }
  }

  PTree::ConstRef getSubtreeForSubId(const std::string &path,
                                            const std::string &subId) const
  {
    PTree::ConstRef r = getSubtree(path);
    r.selfId = joinPaths(selfId, subId);
    return r;
  }

  ConstRef reIDfy(const std::string &subId) const
  {
    ConstRef r = *this;
    r.selfId = joinPaths(selfId, subId);
    return r;
  }

  ConstRef getSubtree(const std::string &path) const
  {
    return getSubtreeImpl<ConstRef>(path);
  }

  std::string const& getPath() const { return selfPath; }
  std::string const& getId() const { return selfId; }

protected:

  ConstRef(PTree &owner,
           const std::string &selfPath,
           const std::string &selfId)
  : owner(&owner),
    selfPath(selfPath),
    selfId(selfId)
  {
    assert(this->owner);
  }

  friend class PTree;

  // using pointers instead of smart pointers for the time being
  // to think less about the underlying data structure
  // (shared_ptr-s to internal nodes would be best, but that requires
  // corresponding data structure design and would disallow the use of ptrees)
  PTree  *owner;        // ptr and not a ref to make it assignable

  std::string selfPath;
  std::string selfId;

  template <typename TSelf>
  TSelf getSubtreeImpl(const std::string &path) const
  {
    assert(owner);
    return TSelf(*owner, joinPaths(selfPath, path), selfId);
  }
};


class PTree::Ref : public PTree::ConstRef
{
public:
  Ref()
  { }

  void setRecord(const std::string &path, PTree::Record const& r)
  {
    assert(owner);
    boost::lock_guard<boost::mutex> g(owner->mutex);
    owner->getRecord(PTree::joinPaths(selfPath, path)) = r;
  }

  template <typename TData>
  void set(const std::string &path, const TData &value) const
  {
    using boost::lexical_cast;
    assert(owner);
    boost::lock_guard<boost::mutex> g(owner->mutex);
    owner->getRecord(PTree::joinPaths(selfPath, path)).set_as<TData>(value);
  }

  void undefine(const std::string &path) const
  {
    using boost::lexical_cast;
    assert(owner);
    boost::lock_guard<boost::mutex> g(owner->mutex);
    owner->getRecord(PTree::joinPaths(selfPath, path)).undefine();
  }

  template <typename TData>
  void setValue(const TData &value) const
  {
    assert(owner);
    boost::lock_guard<boost::mutex> g(owner->mutex);
    // do not record access, for it's slow
    owner->getRecord(selfPath).set_as<TData>(value);
  }

  PTree::Ref getSubtree(const std::string &path) const
  {
    return getSubtreeImpl<Ref>(path);
  }

  PTree::Ref getSubtreeForSubId(const std::string &path,
                                       const std::string &subId) const
  {
    PTree::Ref r = getSubtree(path);
    r.selfId = joinPaths(selfId, subId);
    return r;
  }

  Ref reIDfy(const std::string &subId) const
  {
    Ref r = *this;
    r.selfId = joinPaths(selfId, subId);
    return r;
  }


protected:
  friend class PTree;

  Ref(PTree &owner,
      const std::string &selfPath,
      const std::string &selfId)
  : ConstRef(owner, selfPath, selfId)
  { }
};


inline PTree::ConstRef PTree::root(const std::string &id) const
{
  return const_cast<PTree *>(this)->root(id);
}

inline PTree::Ref PTree::root(const std::string &id)
{
  return Ref(*this, "", id);
}


} // namespace mxprops
