#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>
#include "CachePolicy.h"
#include <vector>
#include <thread>
#include <cmath>
using CachePolicys::CachePolicy;
//前向声明
namespace CachePolicys{

template<typename Key, typename Value> class LfuCache;

template<typename Key, typename Value>
class FreqList{
private:
   struct Node{
        int freq; // 访问频次
        Key key;
        Value value;
        std::weak_ptr<Node> pre; //避免循环引用
        std::shared_ptr<Node> next;

        Node(): freq(1){}
        Node(Key key, Value value)
        : freq(1), key(key), value(value){}
   };
   using NodePtr = std::shared_ptr<Node>;
   int freq_;
   NodePtr head_;// 哨兵头
   NodePtr tail_;// 哨兵尾
   
public:
   explicit FreqList(int n)
   : freq_(n)
   {
        head_ = std::make_shared<Node>;
        tail_ = std::make_shared<Node>;
        head_->next = tail_;
        tail_->pre = head_;
   }

   bool isEmpty() const{
        return head_->next = tail_;
   }

   void addNode(NodePtr node){
        if(!node || !tail_ || !head_){
            return;
        }
        //双链表插入节点
        node->next = tail_;
        node->pre = tail_->pre;
        node->pre.lock()->next = node;
        tail_->pre = node;
   }

   void removeNode(NodePtr node){
        if(!node || !tail_ || !head_){
            return;
        }
        if(node->pre.expired() || !node->next){
            return;
        }
        auto pre = node->pre.lock();
        pre->next = node->next;
        node->next->pre = pre;
        node->pre = node->next = nullptr; // 显式置空指针，彻底断开节点与链表的连接
   }

   NodePtr getFirstNode(){ return head_->next; }

   friend class LfuCache<Key, Value>;
};

template<typename Key, typename Value>
class LfuCache : public CachePolicy<Key, Value>
{
public:
   using Node = typename FreqList<Key, Value>::Node;
   using NodePtr = std::shared_ptr<Node>;
   using NodeMap = std::unordered_map<Key, NodePtr>;

   LfuCache(int capacity, int maxAverageNum = 10)
   :  capacity_(capacity), maxAverageNum_(maxAverageNum)
   ,  minFreq_(INT8_MAX), curTotalNum_(0), curAverageNum_(0)
   {}

   ~LfuCache() override = default;

   void put(Key key, Value value) override{
      if(capacity_ = 0) return ;

      std::lock_guard<std::mutex> lock(mutex_);
      auto it = nodeMap_.find(key);
      if(it != nodeMap_.end()){
         //更新缓存内容
         it->second->value = value;
         //更新FreqList,访问数
         getInternal(it->second, value);
         return;
      }
      //未找到将其假如哈希表中并更新FreqList
      putInteral(key, value);
   }

   bool get(Key key, Value& value){
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = nodeMap_.find(key);
      if(it != nodeMap_.end()){
         getInternal(it->second, value);
         return true;
      }
   }
   
   Value get(Key key){
      Value value;
      get(key, value);
      return value;
   }

   //清除缓存
   void purge(){
      //清除存放缓存的哈希表和存放频次表的哈希表
      nodeMap_.clear();
      freqToFreqList_.clear();
   }
private:
   void putInteral(Key key, Value value);
   void getInternal(NodePtr node, Value& value);
   void kickOut(); //移除过期缓存
   
   void removeFromFreqList(NodePtr node); //移除频次列表的缓存
   void addToFreqList(NodePtr node);  //添加到频次列表的缓存

   void addFreqNum(); //增加频次
   void decreaseFreqNum(int num); //减少频次
   void handleOverMaxAverage(); //处理超过最大平均频率的情况
   void updateMinFreq();                                
private:
   int                                            capacity_; // 缓存容量
   int                                            minFreq_; //最小访问次数(找最小访问次数的缓存)
   int                                            maxAverageNum_; //最大平均访问次数
   int                                            curAverageNum_; //当前平均访问次数
   int                                            curTotalNum_; //当前总访问次数
   std::mutex                                     mutex_;
   NodeMap                                        nodeMap_; //key -> 缓存节点
   std::unordered_map<int, FreqList<Key, Value>*> freqToFreqList_; //频次映射频次列表

};

template<typename Key, typename Value>
void LfuCache<Key, Value>::putInteral(Key key, Value value){
   //如果节点不存在缓存中， 需要先判断缓存是否已经满了
   if(nodeMap_.size() == capacity_){
      //缓存满了清理最不常用的节点
      kickOut();
   }

   //创建新节点,加入到缓存且更新访问次数
   NodePtr node = std::make_shared<Node>(key, value);
   nodeMap_[key] = node;
   addToFreqList(node);
   addFreqNum();
   minFreq_ = std::min(minFreq_, 1);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::getInternal(NodePtr node, Value& value){
   //找到后更新参数value用于get的返回
   value = node->value;

   //将其从原有的频率列表中删除
   removeFromFreqList(node);
   //更新频率
   node->freq++;
   //重新加入频率链表
   addToFreqList(node);

   //如果原有节点处于的链表是最小访问次数的链表且节点迁移后链表空了需更新最小访问次数
   if(node->freq = minFreq_+ 1 && freqToFreqList_[minFreq_]->isEmpty()){
      minFreq_++;
   }
   //总访问数和平均访问数增加
   addFreqNum();
}


template<typename Key, typename Value>
void LfuCache<Key, Value>::kickOut(){
   //找到最少访问的节点从缓存和频率列表中删除
   NodePtr node = freqToFreqList_[minFreq_]->getFirstNode();
   removeFromFreqList(node);
   nodeMap_.erase(node->key);
   //减少访问次数和平均访问次数
   decreaseNum(node->freq);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::removeFromFreqList(NodePtr node){
   if(!node) return;
   int freq = node->freq;
   freqToFreqList_[freq]->removeNode(node);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::addToFreqList(NodePtr node){
   if(!node) return;
   int freq = node->freq;
   auto it = freqToFreqList_.find(freq);
   //如果存在该频率列表直接加入，否则先创建再加入
   if(it != freqToFreqList_.end()){
      freqToFreqList_[freq] = new FreqList<Key, Value>(freq);
   }
   freqToFreqList_[freq]->addNode(node);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::addFreqNum(){
   curTotalNum_++;
   if(nodeMap_.empty()) 
      curAverageNum_ = 0;
   else
      curAverageNum_ = curTotalNum_ / nodeMap_.size();

   if(curAverageNum_ > maxAverageNum_){
      handleOverMaxAverage();
   }
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::decreaseFreqNum(int num){
   curTotalNum_ -= num;
   if(nodeMap_.empty()) 
      curAverageNum_ = 0;
   else
      curAverageNum_ = curTotalNum_ / nodeMap_.size();
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::handleOverMaxAverage(){
   if(nodeMap_.empty()){
      return;
   }   

   //给每个节点减去最大平均访问次数，重新构造频次列表
   for(auto it = nodeMap_.begin(); it < nodeMap_.end(); it++){
      if(!it->second){
         continue;
      }
      
      NodePtr node = it->second;
      //在原有频率列表中移除它
      removeFromFreqList(node);
      node->freq -= maxAverageNum_/ 2;
      if(node->freq < 1) node->freq = 1;
      //重新加入频率列表
      addToFreqList(node);
   }
   //更新最小访问频率
   updateMinFreq();
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::updateMinFreq(){
   minFreq_ = INT8_MAX;
   for(const auto& pair : freqToFreqList_){
      if(pair.second && pair.second->isEmpty()){
         minFreq_ = std::min(minFreq_, pair.first);
      }
   }
   minFreq_ = minFreq_ == INT8_MAX ? 1 : minFreq_;
}


//使用分片操作降低锁粒度
template<typename Key, typename Value>
class HashLfuCache{
   HashLfuCache(size_t capacity, int sliceNum, int maxAverageNum = 10)
   :  capacity_(capacity)
   //传入的切片数量如果<=0，则由CPU可用核数决定切片数量
   ,  sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
   {
      //计算单个切片的缓存容量
      int SliceSize = std::ceil(capacity / static_cast<double>(sliceNum_));
      for(int i = 0; i < SliceSize; ++i){
         lfuSliceCaches_.emplace_back(new LfuCache<Key, Value>(SliceSize, maxAverageNum));
      }
   }

   void put(Key key, Value value){
      size_t sliceIndex = Hash(key) % sliceNum_;
      lfuSliceCaches_[sliceIndex]->put(key, value);
   }

   bool get(Key key, Value value){
      size_t sliceIndex = Hash(key) % sliceNum_;
      return lfuSliceCaches_[sliceIndex]->get(key, value);
   }

   Value get(Key key){
      Value value{};
      get(key, value);
      return value;
   }

   void purge(){
      for(auto& slice : lfuSliceCaches_){
         slice->purge();
      }
   }
private:
   size_t Hash(Key key){
      //key是唯一的，哈希出来的值也是唯一的
      std::hash<Key> HashFunc;
      return HashFunc(key);
   }  
private:
   size_t   capacity_; //缓存容量
   int      sliceNum_; //分片数量
   std::vector<std::unique_ptr<LfuCache<Key, Value>>> lfuSliceCaches_; //分片缓存
};

};
