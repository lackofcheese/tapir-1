#ifndef SOLVER_DISCRETE_OBSERVATIONS_HPP_
#define SOLVER_DISCRETE_OBSERVATIONS_HPP_

#include <iostream>
#include <memory>
#include <unordered_map>

#include "solver/BeliefNode.hpp"

#include "solver/abstract-problem/Model.hpp"

#include "solver/serialization/Serializer.hpp"

#include "ObservationPool.hpp"
#include "ObservationMapping.hpp"


namespace solver {
class ActionPool;
class DiscretePoint;

class ModelWithDiscreteObservations : virtual public solver::Model {
public:
    ModelWithDiscreteObservations() = default;
    virtual ~ModelWithDiscreteObservations() = default;
    _NO_COPY_OR_MOVE(ModelWithDiscreteObservations);

    virtual std::unique_ptr<ObservationPool> createObservationPool() override;
};

class DiscreteObservationPool: public solver::ObservationPool {
  public:
    DiscreteObservationPool() = default;
    virtual ~DiscreteObservationPool() = default;
    _NO_COPY_OR_MOVE(DiscreteObservationPool);

    virtual std::unique_ptr<ObservationMapping>
    createObservationMapping() override;
};

struct DiscreteObservationMapEntry {
    std::unique_ptr<BeliefNode> childNode = nullptr;
    long visitCount = 0;
};

class DiscreteObservationMap: public solver::ObservationMapping {
  public:
    friend class DiscreteObservationTextSerializer;
    DiscreteObservationMap(ActionPool *actionPool);

    // Default destructor; copying and moving disallowed!
    virtual ~DiscreteObservationMap() = default;
    _NO_COPY_OR_MOVE(DiscreteObservationMap);

    virtual BeliefNode *getBelief(Observation const &obs) const override;
    virtual BeliefNode *createBelief(Observation const &obs) override;

    virtual long getNChildren() const override;

    virtual void updateVisitCount(Observation const &obs, long deltaNVisits) override;
    virtual long getVisitCount(Observation const &obs) const override;
    virtual long getTotalVisitCount() const override;
  private:
    ActionPool *actionPool_;

    struct HashContents {
        std::size_t operator()(std::unique_ptr<Observation> const &obs) const {
            return obs->hash();
        }
    };
    struct EqualContents {
        std::size_t operator()(std::unique_ptr<Observation> const &o1,
                std::unique_ptr<Observation> const &o2) const {
            return o1->equals(*o2);
        }
    };
    typedef std::unordered_map<std::unique_ptr<Observation>,
            DiscreteObservationMapEntry, HashContents, EqualContents> ChildMap;
    ChildMap childMap_;

    long totalVisitCount_;
};

class DiscreteObservationTextSerializer: virtual public solver::Serializer {
  public:
    DiscreteObservationTextSerializer() = default;
    virtual ~DiscreteObservationTextSerializer() = default;
    _NO_COPY_OR_MOVE(DiscreteObservationTextSerializer);

    virtual void saveObservationPool(
            ObservationPool const &observationPool, std::ostream &os) override;
    virtual std::unique_ptr<ObservationPool> loadObservationPool(
            std::istream &is) override;
    virtual void saveObservationMapping(ObservationMapping const &map,
            std::ostream &os) override;
    virtual std::unique_ptr<ObservationMapping> loadObservationMapping(
            std::istream &is) override;
};
} /* namespace solver */

#endif /* SOLVER_DISCRETE_OBSERVATIONS_HPP_ */
