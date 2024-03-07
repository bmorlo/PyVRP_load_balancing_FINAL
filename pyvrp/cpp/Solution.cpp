#include "Solution.h"
#include "DurationSegment.h"
#include "LoadSegment.h"
#include "ProblemData.h"

#include <fstream>
#include <numeric>
#include <unordered_map>

using pyvrp::Cost;
using pyvrp::Distance;
using pyvrp::Duration;
using pyvrp::Load;
using pyvrp::Solution;

using Client = size_t;
using Visits = std::vector<Client>;
using Routes = std::vector<Solution::Route>;
using Neighbours = std::vector<std::optional<std::pair<Client, Client>>>;

void Solution::evaluate(ProblemData const &data)
{
    Cost allPrizes = 0;
    for (auto const &client : data.clients())
        allPrizes += client.prize;

    for (auto const &route : routes_)
    {
        // Whole solution statistics.
        numClients_ += route.size();
        prizes_ += route.prizes();
        distance_ += route.distance();
        excessDistance_ += route.excessDistance();
        timeWarp_ += route.timeWarp();
        excessLoad_ += route.excessLoad();
        fixedVehicleCost_ += data.vehicleType(route.vehicleType()).fixedCost;
    }

    uncollectedPrizes_ = allPrizes - prizes_;
}

bool Solution::empty() const { return numClients() == 0 && numRoutes() == 0; }

size_t Solution::numRoutes() const { return routes_.size(); }

size_t Solution::numClients() const { return numClients_; }

size_t Solution::numMissingClients() const { return numMissingClients_; }

Routes const &Solution::routes() const { return routes_; }

Neighbours const &Solution::neighbours() const { return neighbours_; }

bool Solution::isFeasible() const
{
    // clang-format off
    return !hasExcessLoad()
        && !hasTimeWarp()
        && !hasExcessDistance()
        && isComplete();
    // clang-format on
}

bool Solution::isComplete() const { return numMissingClients_ == 0; }

bool Solution::hasExcessLoad() const { return excessLoad_ > 0; }

bool Solution::hasExcessDistance() const { return excessDistance_ > 0; }

bool Solution::hasTimeWarp() const { return timeWarp_ > 0; }

Distance Solution::distance() const { return distance_; }

Load Solution::excessLoad() const { return excessLoad_; }

Distance Solution::excessDistance() const { return excessDistance_; }

Cost Solution::fixedVehicleCost() const { return fixedVehicleCost_; }

Cost Solution::prizes() const { return prizes_; }

Cost Solution::uncollectedPrizes() const { return uncollectedPrizes_; }

Duration Solution::timeWarp() const { return timeWarp_; }

void Solution::makeNeighbours(ProblemData const &data)
{
    for (auto const &route : routes_)
    {
        auto const depot = data.vehicleType(route.vehicleType()).depot;

        for (size_t idx = 0; idx != route.size(); ++idx)
            neighbours_[route[idx]]
                = {idx == 0 ? depot : route[idx - 1],                  // pred
                   idx == route.size() - 1 ? depot : route[idx + 1]};  // succ
    }
}

bool Solution::operator==(Solution const &other) const
{
    // First compare simple attributes, since that's quick and cheap.
    bool const simpleChecks = distance_ == other.distance_
                              && excessLoad_ == other.excessLoad_
                              && timeWarp_ == other.timeWarp_
                              && routes_.size() == other.routes_.size();

    if (!simpleChecks)
        return false;

    // Now test if the neighbours are all equal. If that's the case we have
    // the same visit structure across routes.
    if (neighbours_ != other.neighbours_)
        return false;

    // The visits are the same for both solutions, but the vehicle assignments
    // need not be. We check this via a mapping from the first client in each
    // route to the vehicle type of that route. We need to base this on the
    // visits since the routes need not be in the same order between solutions.
    std::unordered_map<Client, VehicleType> client2vehType;
    for (auto const &route : routes_)
        client2vehType[route.visits()[0]] = route.vehicleType();

    for (auto const &route : other.routes_)
        if (client2vehType[route.visits()[0]] != route.vehicleType())
            return false;

    return true;
}

Solution::Solution(ProblemData const &data, RandomNumberGenerator &rng)
    : neighbours_(data.numLocations(), std::nullopt)
{
    // Shuffle clients (to create random routes)
    auto clients = std::vector<size_t>(data.numClients());
    std::iota(clients.begin(), clients.end(), data.numDepots());
    std::shuffle(clients.begin(), clients.end(), rng);

    // Distribute clients evenly over the routes: the total number of clients
    // per vehicle, with an adjustment in case the division is not perfect and
    // there are not enough vehicles for single-client routes.
    auto const numVehicles = data.numVehicles();
    auto const numClients = data.numClients();
    auto const perVehicle = std::max<size_t>(numClients / numVehicles, 1);
    auto const adjustment
        = numClients > numVehicles && numClients % numVehicles != 0;
    auto const perRoute = perVehicle + adjustment;
    auto const numRoutes = (numClients + perRoute - 1) / perRoute;

    std::vector<std::vector<Client>> routes(numRoutes);
    for (size_t idx = 0; idx != numClients; ++idx)
        routes[idx / perRoute].push_back(clients[idx]);

    routes_.reserve(numRoutes);
    size_t count = 0;
    for (size_t vehType = 0; vehType != data.numVehicleTypes(); ++vehType)
    {
        auto const numAvailable = data.vehicleType(vehType).numAvailable;
        for (size_t i = 0; i != numAvailable; ++i)
            if (count < routes.size())
                routes_.emplace_back(data, routes[count++], vehType);
    }

    makeNeighbours(data);
    evaluate(data);
}

Solution::Solution(ProblemData const &data,
                   std::vector<std::vector<Client>> const &routes)
{
    Routes transformedRoutes;
    transformedRoutes.reserve(routes.size());
    for (auto const &visits : routes)
        transformedRoutes.emplace_back(data, visits, 0);

    *this = Solution(data, transformedRoutes);
}

Solution::Solution(ProblemData const &data, std::vector<Route> const &routes)
    : routes_(routes), neighbours_(data.numLocations(), std::nullopt)
{
    if (routes.size() > data.numVehicles())
    {
        auto const msg = "Number of routes must not exceed number of vehicles.";
        throw std::runtime_error(msg);
    }

    std::vector<size_t> visits(data.numLocations(), 0);
    std::vector<size_t> usedVehicles(data.numVehicleTypes(), 0);
    for (auto const &route : routes)
    {
        if (route.empty())
            throw std::runtime_error("Solution should not have empty routes.");

        usedVehicles[route.vehicleType()]++;
        for (auto const client : route)
            visits[client]++;
    }

    for (size_t client = data.numDepots(); client != data.numLocations();
         ++client)
    {
        ProblemData::Client const &clientData = data.location(client);
        if (clientData.required && visits[client] == 0)
            numMissingClients_ += 1;

        if (visits[client] > 1)
        {
            std::ostringstream msg;
            msg << "Client " << client << " is visited more than once.";
            throw std::runtime_error(msg.str());
        }
    }

    for (size_t vehType = 0; vehType != data.numVehicleTypes(); vehType++)
        if (usedVehicles[vehType] > data.vehicleType(vehType).numAvailable)
        {
            std::ostringstream msg;
            auto const numAvailable = data.vehicleType(vehType).numAvailable;
            msg << "Used more than " << numAvailable << " vehicles of type "
                << vehType << '.';
            throw std::runtime_error(msg.str());
        }

    makeNeighbours(data);
    evaluate(data);
}

Solution::Solution(size_t numClients,
                   size_t numMissingClients,
                   Distance distance,
                   Distance excessDistance,
                   Load excessLoad,
                   Cost fixedVehicleCost,
                   Cost prizes,
                   Cost uncollectedPrizes,
                   Duration timeWarp,
                   Routes const &routes,
                   Neighbours neighbours)
    : numClients_(numClients),
      numMissingClients_(numMissingClients),
      distance_(distance),
      excessDistance_(excessDistance),
      excessLoad_(excessLoad),
      fixedVehicleCost_(fixedVehicleCost),
      prizes_(prizes),
      uncollectedPrizes_(uncollectedPrizes),
      timeWarp_(timeWarp),
      routes_(routes),
      neighbours_(neighbours)
{
}

Solution::Route::Route(ProblemData const &data,
                       Visits visits,
                       size_t const vehicleType)
    : visits_(std::move(visits)), centroid_({0, 0}), vehicleType_(vehicleType)
{
    auto const &vehType = data.vehicleType(vehicleType);
    depot_ = vehType.depot;

    if (visits_.empty())
        return;

    // Time window is limited by both the depot open and closing times, and
    // the vehicle's start and end of shift, whichever is tighter.
    ProblemData::Depot const &depotLocation = data.location(depot_);
    DurationSegment depotDS(depot_,
                            depot_,
                            0,
                            0,
                            std::max(depotLocation.twEarly, vehType.twEarly),
                            std::min(depotLocation.twLate, vehType.twLate),
                            0);

    auto ds = depotDS;
    auto ls = LoadSegment(0, 0, 0);
    size_t prevClient = vehType.depot;

    for (size_t idx = 0; idx != size(); ++idx)
    {
        auto const client = visits_[idx];
        ProblemData::Client const &clientData = data.location(client);

        distance_ += data.dist(prevClient, client);
        travel_ += data.duration(prevClient, client);
        service_ += clientData.serviceDuration;
        prizes_ += clientData.prize;

        centroid_.first += static_cast<double>(clientData.x) / size();
        centroid_.second += static_cast<double>(clientData.y) / size();

        auto const clientDS = DurationSegment(client, clientData);
        ds = DurationSegment::merge(data.durationMatrix(), ds, clientDS);

        auto const clientLs = LoadSegment(clientData);
        ls = LoadSegment::merge(ls, clientLs);

        prevClient = client;
    }

    Client const last = visits_.back();  // last client has depot as successor
    distance_ += data.dist(last, vehType.depot);
    travel_ += data.duration(last, vehType.depot);
    excessDistance_ = std::max<Distance>(distance_ - vehType.maxDistance, 0);

    delivery_ = ls.delivery();
    pickup_ = ls.pickup();
    excessLoad_ = std::max<Load>(ls.load() - vehType.capacity, 0);

    ds = DurationSegment::merge(data.durationMatrix(), ds, depotDS);
    duration_ = ds.duration();
    startTime_ = ds.twEarly();
    slack_ = ds.twLate() - ds.twEarly();
    timeWarp_ = ds.timeWarp(vehType.maxDuration);
    release_ = ds.releaseTime();
}

Solution::Route::Route(Visits visits,
                       Distance distance,
                       Distance excessDistance,
                       Load delivery,
                       Load pickup,
                       Load excessLoad,
                       Duration duration,
                       Duration timeWarp,
                       Duration travel,
                       Duration service,
                       Duration wait,
                       Duration release,
                       Duration startTime,
                       Duration slack,
                       Cost prizes,
                       std::pair<double, double> centroid,
                       size_t vehicleType,
                       size_t depot)
    : visits_(std::move(visits)),
      distance_(distance),
      excessDistance_(excessDistance),
      delivery_(delivery),
      pickup_(pickup),
      excessLoad_(excessLoad),
      duration_(duration),
      timeWarp_(timeWarp),
      travel_(travel),
      service_(service),
      wait_(wait),
      release_(release),
      startTime_(startTime),
      slack_(slack),
      prizes_(prizes),
      centroid_(centroid),
      vehicleType_(vehicleType),
      depot_(depot)
{
}

bool Solution::Route::empty() const { return visits_.empty(); }

size_t Solution::Route::size() const { return visits_.size(); }

Client Solution::Route::operator[](size_t idx) const { return visits_[idx]; }

Visits::const_iterator Solution::Route::begin() const
{
    return visits_.cbegin();
}

Visits::const_iterator Solution::Route::end() const { return visits_.cend(); }

Visits const &Solution::Route::visits() const { return visits_; }

Distance Solution::Route::distance() const { return distance_; }

Distance Solution::Route::excessDistance() const { return excessDistance_; }

Load Solution::Route::delivery() const { return delivery_; }

Load Solution::Route::pickup() const { return pickup_; }

Load Solution::Route::excessLoad() const { return excessLoad_; }

Duration Solution::Route::duration() const { return duration_; }

Duration Solution::Route::serviceDuration() const { return service_; }

Duration Solution::Route::timeWarp() const { return timeWarp_; }

Duration Solution::Route::waitDuration() const
{
    return duration_ - travel_ - service_;
}

Duration Solution::Route::travelDuration() const { return travel_; }

Duration Solution::Route::startTime() const { return startTime_; }

Duration Solution::Route::endTime() const
{
    return startTime_ + duration_ - timeWarp_;
}

Duration Solution::Route::slack() const { return slack_; }

Duration Solution::Route::releaseTime() const { return release_; }

Cost Solution::Route::prizes() const { return prizes_; }

std::pair<double, double> const &Solution::Route::centroid() const
{
    return centroid_;
}

size_t Solution::Route::vehicleType() const { return vehicleType_; }

size_t Solution::Route::depot() const { return depot_; }

bool Solution::Route::isFeasible() const
{
    return !hasExcessLoad() && !hasTimeWarp() && !hasExcessDistance();
}

bool Solution::Route::hasExcessLoad() const { return excessLoad_ > 0; }

bool Solution::Route::hasExcessDistance() const { return excessDistance_ > 0; }

bool Solution::Route::hasTimeWarp() const { return timeWarp_ > 0; }

bool Solution::Route::operator==(Solution::Route const &other) const
{
    // First compare simple attributes, since that's a quick and cheap check.
    // Only when these are the same we test if the visits are all equal.
    // clang-format off
    return distance_ == other.distance_
        && delivery_ == other.delivery_
        && pickup_ == other.pickup_
        && timeWarp_ == other.timeWarp_
        && vehicleType_ == other.vehicleType_
        && visits_ == other.visits_;
    // clang-format on
}

std::ostream &operator<<(std::ostream &out, Solution const &sol)
{
    auto const &routes = sol.routes();

    for (size_t idx = 0; idx != routes.size(); ++idx)
        out << "Route #" << idx + 1 << ": " << routes[idx] << '\n';

    return out;
}

std::ostream &operator<<(std::ostream &out, Solution::Route const &route)
{
    for (Client const client : route)
        out << client << ' ';
    return out;
}
