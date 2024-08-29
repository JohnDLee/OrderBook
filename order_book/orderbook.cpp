#include <cstdint>
#include <vector>
#include <stdexcept>
#include <string>
#include <memory>
#include <list>
#include <functional>
#include <unordered_map>
#include <map>
#include <sstream>
#include <numeric>
#include <iostream>

enum class OrderType
{
    GoodTillCancel,
    FillAndKill,
};

enum class Side
{
    Buy,
    Sell,
};

using Price = float;
using Quantity = std::uint32_t;
using OrderID = std::int32_t;

struct LevelInfo
{
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderbookLevelInfos
{
public:
    OrderbookLevelInfos(const LevelInfos &bids_, const LevelInfos &asks_)
        : bids(bids_)
        , asks(asks_)
    {}


private:
    LevelInfos bids;
    LevelInfos asks;
};


class Order
{
public:
    Order(OrderType order_type_, OrderID order_id_, Side side_, Price price_, Quantity quantity_)
        : order_type(order_type_)
        , order_id(order_id_)
        , side(side_)
        , price(price_)
        , initial_qty(quantity_)
        , remaining_qty(quantity_)
    {}

    OrderType GetOrderType() const {return order_type;}
    OrderID GetOrderID() const {return order_id;}
    Side GetSide() const {return side;}
    Price GetPrice() const {return price;}
    Quantity GetInitialQty() const {return initial_qty;}
    Quantity GetRemainingQty() const {return remaining_qty;}
    Quantity GetFilledQty() const {return (GetInitialQty() - GetRemainingQty());}
    bool isFilled() const {return GetRemainingQty() == 0;}

    void Fill(Quantity quantity)
    {
        if (quantity > GetRemainingQty())
        {
            std::stringstream ss;
            ss << "Order (" << GetOrderID() << ") cannot be filled for more than its remaining quantity.";
            throw std::logic_error(ss.str());
        }
        remaining_qty -= quantity;
    }


private:
    OrderType order_type;
    OrderID order_id;
    Side side;
    Price price;
    Quantity initial_qty;
    Quantity remaining_qty;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify
{
public:
    OrderModify(OrderID order_id_, Side side_, Price price_, Quantity quantity_)
        : order_id(order_id_)
        , side(side_)
        , price(price_)
        , quantity(quantity_)
    {}

    OrderID GetOrderID() const {return order_id;}
    Side GetSide() const {return side;}
    Price GetPrice() const {return price;}
    Quantity GetQty() const {return quantity;}

    OrderPointer ToOrderPointer(OrderType order_type) const
    {
        return std::make_shared<Order>(order_type, GetOrderID(), GetSide(), GetPrice(), GetQty());
    }

private:

    OrderID order_id;
    Side side;
    Price price;
    Quantity quantity;

};




struct TradeInfo
{
    OrderID order_id;
    Price price;
    Quantity quantity;
};

class Trade
{
public:
    Trade(const TradeInfo &bid_trade_, const TradeInfo & ask_trade_)
        : bid_trade(bid_trade_)
        , ask_trade(ask_trade_)
    {}

    const TradeInfo& GetBidTrade() const {return bid_trade;}
    const TradeInfo& GetAskTrade() const {return ask_trade;}

private:
    TradeInfo bid_trade;
    TradeInfo ask_trade;

};

using Trades = std::vector<Trade>;


class OrderBook
{
public:
    Trades AddOrder(OrderPointer order)
    {
        if (orders.contains(order->GetOrderID()))
            return {};

        if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
            return {};

        OrderPointers::iterator iterator;

        if (order->GetSide() == Side::Buy)
        {
            auto& orders_ = bids[order->GetPrice()];
            orders_.push_back(order);
            iterator = std::next(orders_.begin(), static_cast<std::ptrdiff_t>(orders_.size() - 1));
        }
        else
        {
            auto& orders_ = asks[order->GetPrice()];
            orders_.push_back(order);
            iterator = std::next(orders_.begin(), static_cast<std::ptrdiff_t>(orders_.size() - 1));
        }

        orders.insert({ order->GetOrderID(), OrderEntry { order, iterator}});
        return MatchOrders();
    }

    void CancelOrder(OrderID order_id)
    {
        if (!orders.contains(order_id)) return;


        const auto& [order, order_iter] = orders.at(order_id);
        orders.erase(order_id);

        if (order->GetSide() == Side::Sell)
        {
            auto price = order->GetPrice();
            auto &orders_ = asks.at(price);
            orders_.erase(order_iter);
            if (orders_.empty())
                asks.erase(price);
            std::cout << "Test1"<< std::endl;
        }
        else
        {
            auto price = order->GetPrice();
            auto &orders_ = bids.at(price);
            orders_.erase(order_iter);
            if (orders_.empty())
                bids.erase(price);
        }


    }

    Trades MatchOrders(OrderModify order)
    {
        if (!orders.contains(order.GetOrderID())) return {};

        const auto& [existing_order,_] = orders.at(order.GetOrderID());
        CancelOrder(order.GetOrderID());
        return AddOrder(order.ToOrderPointer(existing_order->GetOrderType()));
    }

    std::size_t Size() const  { return orders.size(); }

    OrderbookLevelInfos GetOrderInfos() const
    {
        LevelInfos bidInfos, askInfos;
        bidInfos.reserve(orders.size());
        askInfos.reserve(orders.size());

        auto CreateLevelInfos = [](Price price, const OrderPointers& orders_)
        {
            return LevelInfo( price, std::accumulate(orders_.begin(), orders_.end(), (Quantity) 0,
                            [](std::size_t running_sum, const OrderPointer& order)
                            { return running_sum + order->GetRemainingQty();}));
        };

        for (const auto& [price, orders_] : bids)
            bidInfos.push_back(CreateLevelInfos(price, orders_));

        for (const auto& [price, orders_] : asks)
            askInfos.push_back(CreateLevelInfos(price, orders_));

        return OrderbookLevelInfos {bidInfos, askInfos};
    }

private:


    struct OrderEntry
    {
        OrderPointer order{nullptr};
        OrderPointers::iterator location;
    };

    std::map<Price, OrderPointers, std::greater<Price>> bids;
    std::map<Price, OrderPointers, std::less<Price>> asks;
    std::unordered_map<OrderID, OrderEntry> orders;

    bool CanMatch(Side side, Price price) const
    {
        if (side == Side::Buy)
        {
            if (asks.empty())
                return false;

            const auto& [best_ask, _] = *asks.begin();
            return price >= best_ask;
        }
        else
        {
            if (bids.empty())
                return false;

            const auto& [best_bid, _] = *bids.begin();
            return price <= best_bid;
        }
    }

    Trades MatchOrders()
    {
        Trades trades;
        trades.reserve(orders.size());

        while (true)
        {
            if (bids.empty() || asks.empty()) break;

            auto &[bid_price, bids_] = *bids.begin();
            auto &[ask_price, asks_] = *asks.begin();

            if (bid_price < ask_price) break;

            while (bids_.size() && asks_.size())
            {
                auto& bid = bids_.front();
                auto& ask = asks_.front();

                Quantity quantity = std::min(bid->GetRemainingQty(), ask->GetRemainingQty());

                bid->Fill(quantity);
                ask->Fill(quantity);

                if (bid->isFilled())
                {
                    bids_.pop_front();
                    orders.erase(bid->GetOrderID());
                }

                if (ask->isFilled())
                {
                    asks_.pop_front();
                    orders.erase(ask->GetOrderID());
                }

                if (bids_.empty())
                    bids.erase(bid_price);
                if (asks_.empty())
                    asks.erase(ask_price);

                trades.push_back(Trade {
                    TradeInfo { bid->GetOrderID(), bid->GetPrice(), quantity },
                    TradeInfo { ask->GetOrderID(), ask->GetPrice(), quantity }
                    }
                );
            }
        }
        if (!bids.empty())
        {
            auto& [_, bids_] = *bids.begin();
            auto& order = bids_.front();
            if (order->GetOrderType() == OrderType::FillAndKill)
                CancelOrder(order->GetOrderID());
        }

        if (!asks.empty())
        {
            auto& [_, asks_] = *asks.begin();
            auto& order = asks_.front();
            if (order->GetOrderType() == OrderType::FillAndKill)
                CancelOrder(order->GetOrderID());
        }

        return trades;
    }
};

int main()
{
    OrderBook orderbook;
    const OrderID order_id = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, order_id, Side::Buy, 100, 10));
    std::cout << orderbook.Size() << std::endl;
    orderbook.CancelOrder(order_id);
    std::cout << orderbook.Size() << std::endl;
    return 0;
}
