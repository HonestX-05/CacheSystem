#pragma once

#include "ArcCacheNode.h"
#include <mutex>
#include <unordered_map>
#include <list>

namespace CachePolicys{

template<typename Key, typename Value>

class ArcLfuPart{
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    using FreqMap = std::unordered_map<size_t, std::list<NodePtr>>;

    explicit ArcLfuPart(size_t capacity, size_t transformThreshold)
    :   capacity_(capacity)
    ,   ghostCapacity_(capacity)
    ,   transformThreshold_(transformThreshold)
    ,   minFreq_(0)
    {
        initializeLists();
    }

    bool put(Key key, Value value){
        if(capacity_ == 0) return false;
        
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if(it != mainCache_.end()){
            return updateExistingNode(it->second, value);
        }
        return addNewNode(key, value);

    }

    bool get(Key key, Value& value){
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if(it != mainCache_.end()){
            updateNodeFrequency(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    bool contain(Key key) { return mainCache_.find(key) != mainCache_.end(); }

    bool checkGhost(Key key){
        auto it = ghostCache_.find(key);
        if(it != ghostCache_.end()){
            removeFromGhost(it->second);
            ghostCache_.erase(it);
            return true;
        }
        return false;
    }

    void increaseCapacity() { ++capacity_; }

    bool decreaseCapacity(){
        if(capacity_ <= 0) return false;
        if(mainCache_.size() == capacity_){
            evictLeastFrequent();
        }
        --capacity_;
        return true;
    }
private:
    void initializeLists(){
        ghostHead_ = std::make_shared<NodePtr>;
        ghostTail_ = std::make_shared<NodePtr>;
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_ = ghostHead_;
    }

    bool updateExistingNode(NodePtr node, Value& value){
        node->setValue(value);
        updateNodeFrequency(node);
        return true;
    }

    bool addNewNode(const Key& key,const Value& value){
        if(mainCache_.size() >= capacity_){
            evictLeastFrequent();
        }

        NodePtr newNode = std::make_shared<NodePtr>(key, value);
        mainCache_[key] = newNode;

        if(freqMap_.find(1) != freqMap_.end()){
            freqMap_[1] = std::list<NodePtr>();
        }
        freqMap_[1].push_back(newNode);
        minFreq_ = 1;

        return true;
    }

    void updateNodeFrequency(NodePtr node){
        size_t oldFreq = node->getAccessCount();
        node->incrementAccessCount();
        size_t newFreq = node->getAccessCount();

        auto& oldList = freqMap_[oldFreq];
        oldList.remove(node);
        //处理移除后链表为空的情况
        if(oldList.empty()){
            freqMap_.erase(oldFreq);
            //如果该链表的缓存是最小访问频次的缓存，更新最小访问频次
            if(oldFreq == minFreq_){
               ++minFreq_;
            }
        }

        //添加到新的链表
        if(freqMap_.find(newFreq) == freqMap_.end()){
            freqMap_[newFreq] = std::list<NodePtr>();
        }
        freqMap_[newFreq].push_back(node);
    }

    void evictLeastFrequent(){
        if(freqMap_.empty()) return;

        //获取最小频率链表
        auto& minFreqList = freqMap_[minFreq_];
        if(minFreqList.empty()){
            return;
        }
        
        NodePtr leastNode = minFreqList.front();
        minFreqList.pop_front();
        
        //如果频率链表为空，删除映射
        if(minFreqList.empty()){
            freqMap_.erase(minFreq_);
            //更新最小频率
            if(!freqMap_.empty()){
                minFreq_ = freqMap_.begin()->first;
            }
        }
        
        if(ghostCache_.size() >= ghostCapacity_){
            removeOldestGhost();
        }
        addToGhost(leastNode);
        mainCache_.erase(leastNode->getKey());
   }

    void removeFromGhost(NodePtr node){
        if(!node->prev_.expired() && node->next_){
            auto prev = node->prev.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->prev_ = node->next_ = nullptr;
        }   
    }

    void addToGhost(NodePtr node){
        node->next_ = ghostTail_;
        node->prev_ = ghostTail_->prev_;
        if(!ghostTail_->prev_.expired()){
            ghostTail_->prev_.lock()->next_ = node;
        }
        ghostTail_->prev = node;
        ghostCache_[node->getKey()] = node;  
    }

    void removeOldestGhost(){
        NodePtr oldestNode = ghostHead_->next_;
        if(oldestNode != ghostTail_){
            removeFromGhost(oldestNode);
            ghostCache_.erase(oldestNode->getKey());
        }
    }
private:
    size_t  capacity_;
    size_t  ghostCapacity_;
    size_t  transformThreshold_;
    size_t  minFreq_;
    std::mutex  mutex_;

    NodeMap  mainCache_;
    NodeMap  ghostCache_;
    FreqMap  freqMap_;

    NodePtr  ghostHead_;
    NodePtr  ghostTail_;
};

};
