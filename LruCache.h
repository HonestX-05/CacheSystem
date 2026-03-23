#pragma once 

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>

#include "CachePolicy.h"

namespace CachePolicys
{

// 前向声明
template<typename Key, typename Value> class LruCache;

template<typename Key, typename Value>
class LruNode 
{
private:
    Key key_;
    Value value_;
    size_t accessCount_;  // 访问次数
    std::weak_ptr<LruNode<Key, Value>> prev_;  // 改为weak_ptr打破循环引用
    std::shared_ptr<LruNode<Key, Value>> next_;

public:
    LruNode(Key key, Value value)
        : key_(key)
        , value_(value)
        , accessCount_(1) 
    {}

    // 提供必要的访问器
    Key getKey() const { return key_; }
    Value getValue() const { return value_; }
    void setValue(const Value& value) { value_ = value; }
    size_t getAccessCount() const { return accessCount_; }
    void incrementAccessCount() { ++accessCount_; }

    friend class LruCache<Key, Value>;
};


template<typename Key, typename Value>
class LruCache : public CachePolicy<Key, Value>
{
public:
    using LruNodeType = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    LruCache(int capacity)
        : capacity_(capacity)
    {
        initializeList();
    }

    ~LruCache() override = default;

    // 添加缓存
    void put(Key key, Value value) override
    {
        if (capacity_ <= 0)
            return;
    
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            // 如果在当前容器中,则更新value,并调用get方法，代表该数据刚被访问
            updateExistingNode(it->second, value);
            return ;
        }

        addNewNode(key, value);
    }

    bool get(Key key, Value& value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            moveToMostRecent(it->second);
            value = it->second->getValue;
            return true;
        }

        return false;
    }

    Value get(Key key) override
    {
        Value value{};
        get(key, value);
        return value;
    }
    
    //删除指定元素
    void remove(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            removeNode(it->second);
            nodeMap_.erase(it);
        }
    }

private:
    void initializeList()
    {
        //创建首尾节点
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }
    
    
    void updateExistingNode(NodePtr node, const Value& value)
    {
        node->setValue(value);
        moveToMostRecent(node);
    }

    void addNewNode(const Key& key, const Value& value)
    {
        if(nodeMap_.size() >= capacity_)
        {
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode;
    }

    void moveToMostRecent(NodePtr node)
    {
       //断开连接
       removeNode(node);
       //插入到尾部
       insertNode(node);
    }

    void removeNode(NodePtr node)
    {
        //只有头尾节点都存在时才能移除
        //只存在一个的是哨兵节点
        if(!node->prev_.expried() && node->next_)
        {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = prev;
            node->next_ = nullptr;//设置为nullptr，断开连接
        }
    }

    void insertNode(NodePtr node)
    {
        node->next_ = dummyTail_;
        node->prev_ = dummyTail_->prev_;
        dummyTail_->prev_.lock()->next_ = node;
        dummyTail_->prev_ = node;
    }
    
    //驱逐最近最少访问
    void evictLeastRecent()
    {
        NodePtr leastRecent = dummyHead_->next;
        removeNode(leastRecent);
        nodeMap_.erase(leastRecent->getKey());
    }
private:
    int         capacity_;  //缓存容量
    NodeMap     nodeMap_;   //缓存表 key -> Node
    std::mutex  mutex_;
    NodePtr     dummyHead_; //哨兵头
    NodePtr     dummyTail_; //哨兵尾
};

//LRU优化: LRU-k
template<typename Key, typename Value>
class LruKCache : public LruCache<Key, Value>
{
public:
    LruKCache(int capacity, int historyCap, int k)
        :   LruCache<Key, Value>(capacity)
        ,   historyList_(std::make_unique<LruCache<Key, Value>>(historyCap))
        ,   k_(k)
    {}
    Value get(Key key)
    {
        //尝试从主缓存中获取缓存
        Value value{};
        bool exmace = LruCache<Key, Value>::get(key, value); //exist main cache
        
        //在历史列表里查找并更新
        size_t historyCount = historyList_->get(key);
        historyCount++;
        historyList_->put(key, historyCount);
        
        
        //命中直接返回
        if(exmace)
        {
            return value;
        }
        
        
        //当历史次数达到k次加入主缓存
        if(historyCount >= k_)
        {   
            //检查历史值是否有记录
            auto it = historyValueMap_[key];
            if(it != historyValueMap_.end())
            {   
                //加入主缓存
                this->put(key, it->second);
                //记录缓存用于返回
                Value storedvalue = it->second;
                //从历史列表，历史记录中移除这条缓存
                historyList_->remove(key);
                historyValueMap_.erase(it);

                return storedvalue;
            }
            else
            {
                //如果历史值没有记录，从历史列表删除这条记录
                historyList_->remove(key);
            }
        }
        //历史记录不足k次返回默认值
        return value;
    }

    
    void put(Key key, Value value) 
    {
        // 检查是否已在主缓存
        Value existingValue{};
        bool inMainCache = LruCache<Key, Value>::get(key, existingValue);
        
        if (inMainCache) 
        {
            // 已在主缓存，直接更新
            LruCache<Key, Value>::put(key, value);
            return;
        }
        
        // 获取并更新访问历史
        size_t historyCount = historyList_->get(key);
        historyCount++;
        historyList_->put(key, historyCount);
        
        // 保存值到历史记录映射，供后续get操作使用
        historyValueMap_[key] = value;
        
        // 检查是否达到k次访问阈值
        if (historyCount >= k_) 
        {
            // 达到阈值，添加到主缓存
            historyList_->remove(key);
            historyValueMap_.erase(key);
            LruCache<Key, Value>::put(key, value);
        }
    }
    
     
private:
    int                                      k_; // 进入主缓存的评判等级
    std::unique_ptr<LruCache<Key, size_t>>   historyList_; // 未达k次的缓存
    std::unordered_map<Key, Value>           historyValueMap_; // 存储未达k次的缓存
};

//哈希分片: 优化锁粒度过大
template<typename Key, typename Value>
class HashLruCache 
{
public:
    HashLruCache(int cap, int sliceNum)
        :   capacity_(cap)
            //当切片数 > 0 则切切片数，否则切当前线程数量
        ,   sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
        {
            size_t slinum = std::ceil(cap / (double) sliceNum);
            for(int i = 0; i < sliceNum_; i++)
            {
                lruSliceCache_.emplace_back(new KLruCache<Key, Value>(slinum));
            }
        }

    void put(Key key, Value value)
    {
        //找到对应分片,取模保证在分片内
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCache_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value& value)
    {
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCache_[sliceIndex]->get(key, value);
    }

    Value get(Key key)
    {
        Value value{};
        get(key, value);
        return value;
    }
private:
    //将key转换为对应哈希值
    size_t Hash(Key key)
    {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }
private:
    int                                                 capacity_;//容量
    int                                                 sliceNum_;//分片数量
    std::vector<std::unique_ptr<LruCache<Key, Value>>>  lruSliceCache_;//切片总缓存
    
};


}//namespace LruCache
