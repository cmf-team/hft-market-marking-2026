#pragma once

#include "common/Exchange.hpp"
#include "common/Types.hpp"

#include <string>

namespace cmf
{

class Strategy
{
  public:
    virtual ~Strategy() = default;

    virtual void onTrade(const Trade& trade, Exchange& exchange) = 0;
    virtual void onLobUpdate(const LOBSnapshot& lob, Exchange& exchange) = 0;
    virtual void onFill(const Fill& fill) = 0;

    virtual std::string name() const = 0;
};

} // namespace cmf
