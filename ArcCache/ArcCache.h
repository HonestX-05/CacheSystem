#pragma once

#include "../CachePolicy.h"
#include "ArcLruPart.h"
#include "ArcLfuPart.h"

namespace CachePolicys{
template<typename Key, typename Value>
class ArcCache : public CachePolicy<Key, Value>{
public:
    explicit ArcCache(size_t capacity = 10, size_t transformThreshold = 2)
    :   capacity_(capacity)
    ,   transformThreshold_(transformThreshold)
    ,   lruPart_(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold))
    ,   lfuPart_(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold))
    {}

    ~ArcCache() override = default;

    void put(Key key, Value value) override{
        checkGhostCaches(key);

        bool inLfu = lfuPart_->contain(key);
        //更新lru缓存
        lruPart_->put(key, value);
        //检查lfu是否要更新
        if(inLfu){
            lfuPart_->put(key, value);
        }
    }

    bool get(Key key, Value& value) override{
        checkGhostCaches(key);
        
        bool isTransform = false;
        if(lruPart_->get(key, value, isTransform)){
            if(isTransform){
                lfuPart_->get(key, value);
            }
            return true;
        }
        return lfuPart_->get(key, value);
    }

    Value get(Key key){
        Value value{};
        get(key, value);
        return value;
    }
private:
    bool checkGhostCaches(Key key){
        bool inGhost = false;
        
        if(lruPart_->checkGhost(key)){
            if(lfuPart_->decreaseCapacity()){
                lruPart_->increaseCapacity();
            }
            inGhost =  true;
        }else if(lfuPart_->checkGhost(key)){
            if(lruPart_->decreaseCapacityt()){
                lfuPart_->increaseCapacity();
            }
            inGhost = true;
        }
        return inGhost;

    }
private:
    size_t  capacity_;
    size_t  transformThreshold_;
    std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;
    std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
};

};
