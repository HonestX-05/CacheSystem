#pragma once

#include "ArcCacheNode.h"
#include <unordered_map>
#include <mutex>

namespace CachePolicys{

template<typename Key, typename Value>
class ArcLruPart{
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    
    explicit ArcLruPart(int capacity, int ghostCapacity, int transfromThreshold)
    :   capacity_(capacity)
    ,   ghostCapacity_(ghostCapacity)
    ,   transfromThreshold_(transfromThreshold)
    {
        initializeLists();
    }

    bool put(Key key, Value value){
        if(capacity_ == 0) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCahce_.find(key);
        if(it != mainCahce_.end()){
            return updateExistingNode(it->value, value);
        }
        return addNewNode(key, value);
    }

    bool get(Key key, Value& value, bool& shouldTransform){
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCahce_.find(key);
        if(it != mainCahce_.end()){
            shouldTransform = updateNodeAccess(it->second);
            it->second->value = value;
            return true;
        }
        return false;
    }

    bool CheckGhost(Key key){
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = ghostCache_.find(key);
        if(it != ghostCache_.end()){
            removeFromGhost(key);
            ghostCache_.erase(it);
            return true;
        }
        return false;
    }

    void increaseCapacity(){ ++capacity_; }

    bool decreaseCapacity(){
        if(capacity_ == 0) return false;
        if(mainCahce_.size() >= capacity_){
            evictLeastRecent();
        }
        --capacity_;
        return true;
    }
private:
    void initializeLists(){
       mainHead_ = std::make_shared<NodeType>; 
       mainTail_ = std::make_shared<NodeType>;
       mainHead_->next_ = mainTail_;
       mainTail_->prev_ = mainHead_;
       
       ghostHead_ = std::make_shared<NodeType>; 
       ghostTail_ = std::make_shared<NodeType>;
       ghostHead_->next_ = ghostTail_;
       ghostTail_->prev_ = ghostHead_;
    }

    bool updateExistingNode(NodePtr node, const Value& value){
        node->setValue(value);
        moveToFront(node);
        return true;
    }

    void moveToFront(NodePtr node){
        if(!node->prev_.expired() && node->tail_){
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->prev_ = node->next = nullptr;
        }
        addToFront(node);
    }

    void addToFront(NodePtr node){
        node->prev_ = mainHead_;
        node->next = mainHead_->next_;
        mainHead_->next->prev_ = node;
        mainHead_->next_ = node;
    }

    bool addNewNode(const Key& key, const Value& value){
        if(mainCahce_.size() >= capacity_){
            evictLeastRecent(); // 驱逐最近最少访问
        }
        NodePtr newNode = std::make_shared<NodeType>(key, value);
        mainCahce_[key] = newNode;
        addToFront(newNode);
        return true;
    } 

    void evictLeastRecent(){
        NodePtr leastRecent = mainTail_->prev_.lock();
        if(!leastRecent || leastRecent == mainHead_){
            return;
        }

        //从主链表中移除
        removeFromMain(leastRecent);

        //加入淘汰链表
        if(ghostCache_.size() >= ghostCapacity_){
            removeOldestGhost();
        }
        addToGhost(leastRecent);

        //主缓存映射中移除
        mainCahce_.erase(leastRecent->getKey());
    }

    void removeFromMain(NodePtr node){
       if(!node->prev_.expired() && node->next_){
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->prev = node->next = nullptr;
       }
    }

    void removeOldestGhost(){
        NodePtr OldestGhost = ghostTail_.prev.lock();
        if(!OldestGhost || OldestGhost = ghostHead_){
            return ;
        }
        removeFromMain(OldestGhost);
        ghostCache_.erase(OldestGhost->getKey());
    }

    void removeFromGhost(NodePtr node){
        if(!node->prev_.expired() && node->next_){
            auto prev = node->prev.lock();
            prev->next_ = node->next;
            node->next_->prev_ = node->prev_;
            node->prev_ = node->next_ = nullptr;
        }
    }

    void addToGhost(NodePtr node){
        node->accessCount_ = 1;

        //加入Ghost链表头部
        node->next_ = ghostHead_->next_;
        node->prev_ = ghostHead_; 
        node->next_->prev_ = node;
        ghostHead_->next_ = node;

        ghostCache_[node->getKey()] = node;
    }

    bool updateNodeAccess(NodePtr node){
        moveToFront(node);
        node->incrementAccessCount();
        return node->getAccessCount() >= transfromThreshold_;
    }
private:
    size_t capacity_;
    size_t ghostCapacity_;
    size_t transfromThreshold_; //转换阈值
    std::mutex mutex_;

    NodeMap mainCahce_; // key -> nodeptr
    NodeMap ghostCache_;

    //主链表
    NodePtr mainHead_;
    NodePtr mainTail_;
    //淘汰链表
    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

};
