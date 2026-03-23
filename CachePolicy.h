#pragma once

namespace CachePolicys{

template<typename Key, typename Value>
class CachePolicy
{
public:
    //virtual关键字支持子类重载
    virtual ~CachePolicy() {};

    //添加缓存接口，定义纯虚函数，要求子类必须实现
    virtual void put(Key key, Value value) = 0;

    //key作为传入参数，value是传入传出函数
    //能查找到节点返回true，不能查到节点返回false
    virtual bool get(Key key, Value& value) = 0;
    
    //能查到就直接返回其值
    virtual Value get(Key key);
};

}
