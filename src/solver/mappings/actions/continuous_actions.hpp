/** @file continuous_actions.hpp
 *
 * Provides a default implementation for an action mapping that uses a set of continuous or hybrid actions,
 * i.e. there is set of action categories, and the actions in each of these
 * categories will map to the same child nodes in the belief tree.
 *
 * Continuous actions need to be constructed using action construction data which is a vector representation
 * that is interpreted by the chooser.
 *
 * There is also support for additional discrete actions to be added to the set of actions.
 *
 * The mapping class stores the entries in an unordered_map indexed by the construction data for fast retrieval
 *
 *
 * This involves subclasses of the following abstract classes:
 * -ActionPool
 * -ActionMapping
 * -ActionMappingEntry
 *
 * as well as a serialization class providing methods to serialize this particular kind of
 * action pool and action mapping.
 */
#ifndef SOLVER_CONTINUOUS_ACTIONS_HPP_
#define SOLVER_CONTINUOUS_ACTIONS_HPP_

#include <memory>
#include <vector>

#include "global.hpp"
#include "LinkedHashSet.hpp"

#include "solver/serialization/Serializer.hpp"
#include "solver/abstract-problem/Action.hpp"
#include "solver/abstract-problem/Model.hpp"

#include "solver/ActionNode.hpp"

#include "solver/mappings/actions/ActionPool.hpp"
#include "solver/mappings/actions/ActionMapping.hpp"

namespace solver {
class ContinuousActionMap;
class ContinuousActionMapEntry;

/** An abstract class that contains the data to construct continuous actions.
 *
 * The data has to be in vector form. The interface is storage and size agnostic.
 * As such the constructiondata is only accessed though the data() function which
 * returns a pointer to the array data of an std::array or std::vector or similar.
 *
 * The data() pointer only needs to point to the part relevant for the continuous
 * action space. Additional discrete actions may be handled otherwise.
 *
 * Note, that a chooser for continuous actions is likely to make assumptions about
 * the size of data(). It is expected that there is one value for each dimension.
 *
 */
class ContinuousActionConstructionDataBase {
	virtual ~ContinuousActionConstructionDataBase() = default;

	/** Returns a pointer to the data array of the underlying vector. If implemented using an std::array, just return std::array::data() */
	virtual const double* data() const = 0;

};

/** An abstract class for continuous actions.
 *
 * An implementation should keep a copy of the construction data so a reference to it can
 * be provided when needed.
 */
class ContinuousAction: public solver::Action {
public:
	virtual const ContinuousActionConstructionDataBase& getConstructionData() const = 0;
};


/** An abstract service class for ContinuousActionMapp to store actions.
 *
 * Actions are stored in this container and indexed by the construction data.
 *
 * This is meant to be implemented as unodered_map.
 *
 * Implementations can tweak the hashing and equality functions used to create an
 * equivalence relation for very similar actions.
 *
 */
class ContinuousActionContainerBase {
public:
	virtual ~ContinuousActionContainerBase() = default;
	_NO_COPY_OR_MOVE(ContinuousActionContainerBase);

	virtual std::unique_ptr<ContinuousActionMapEntry>& at(const ContinuousActionConstructionDataBase& key) = 0;
	virtual const std::unique_ptr<ContinuousActionMapEntry>& at(const ContinuousActionConstructionDataBase& key) const = 0;
	virtual std::unique_ptr<ContinuousActionMapEntry>& operator[](const ContinuousActionConstructionDataBase& key) = 0;
	virtual std::vector<ActionMappingEntry const*> getEntriesWithChildren() const = 0;
	virtual std::vector<ActionMappingEntry const*> getEntriesWithNonzeroVisitCount() const = 0;
};

/** An implementation of ContinuousActionContainerBase as template.
 *
 * It uses CONSTRUCTION_DATA::hash() and CONSTRUCTION_DATA::equal() to compare the keys.
 */
template<class CONSTRUCTION_DATA>
class ContinuousActionContainer: public ContinuousActionContainerBase {
	typedef CONSTRUCTION_DATA KeyType;

	/** Service class so the unordered_map can access hash() and equal(). */
	struct Comparator {
		size_t operator()(const KeyType& key) { return key.hash(); }
		size_t operator()(const KeyType& first, const KeyType& second) { return first.equal(second); }
	};

public:
	virtual std::unique_ptr<ContinuousActionMapEntry>& at(const ContinuousActionConstructionDataBase& key) override;
	virtual const std::unique_ptr<ContinuousActionMapEntry>& at(const ContinuousActionConstructionDataBase& key) const override;
	virtual std::unique_ptr<ContinuousActionMapEntry>& operator[](const ContinuousActionConstructionDataBase& key) override;
	virtual std::vector<ActionMappingEntry const*> getEntriesWithChildren() const override;
	virtual std::vector<ActionMappingEntry const*> getEntriesWithNonzeroVisitCount() const override;
private:
	std::unordered_map<CONSTRUCTION_DATA, std::unique_ptr<ContinuousActionMapEntry>, Comparator, Comparator> container;
};





/** An abstract implementation of the ActionPool interface that considers continuous actions
 *
 * A concrete implementation of this abstract class requires implementations for ...
 */
class ContinuousActionPool: public solver::ActionPool {
    friend class ContinuousActionMap;
  public:
    ContinuousActionPool() = default;
    virtual ~ContinuousActionPool() = default;
    _NO_COPY_OR_MOVE(ContinuousActionPool);


    /** Returns a ContinuousActionMap for the given belief node. */
    virtual std::unique_ptr<ActionMapping> createActionMapping(BeliefNode *node) override;

    /** Returns a container to store actions within a ContinuousActionMap */
    virtual std::unique_ptr<ContinuousActionContainerBase> createActionContainer(BeliefNode *node) const = 0;

    /** Returns an action construction data object based on a vector of numbers that was provided.
     *
     * Here, constructionData is a pointer to a data array as it is returned by
     * ContinuousActionConstructionDataBase::data(). It enables the action chooser to
     * create new actions based on values it seems fit.
     */
    virtual std::unique_ptr<ContinuousActionConstructionDataBase> createActionConstructionData(const double* constructionDataVector, const BeliefNode* belief) const = 0;

    /** Returns an action based on the Construction Data that was provided.
     *
     * In this version, constructionData is a pointer to a data array as it is returned by
     * ContinuousActionConstructionDataBase::data(). It enables the action chooser to
     * create new actions based on values it seems fit.
     *
     * The default version uses createFullActionConstructionData first and then creates an action based
     * on the full construction data. This might be inefficient and an implementation can override
     * this function for a more direct approach.
     *
     * TODO: Check whether this function is actually used or can be removed.
     */
    virtual std::unique_ptr<Action> createAction(const double* constructionDataVector, const BeliefNode* belief) const;


    /** Returns an action based on the Construction Data that was provided.
     *
     * The default version calls createAction(constructionData.data()) which is probably fine
     * in a purely continuous case, but probably not in a hybrid case.
     */
    virtual std::unique_ptr<Action> createAction(const ContinuousActionConstructionDataBase& constructionData) const = 0;


    /** Returns a shared pointer to a container containing the construction data for the additional fixed actions in a hybrid action space.
     *
     * The result is a shared pointer. Thus, the implementation can decide whether it wants to create the container and pass on ownership or it
     * can return a reference to an internal vector without having to re-create it every time.
     *
     * The default version returns null to indicate there are no fixed actions.
     */
	virtual std::shared_ptr<const std::vector<ContinuousActionConstructionDataBase>> createFixedActions(const BeliefNode* belief) const;


	/** This acts as a hint whether the chooser should try the fixed actions in the sequence they are given
	 * or randomise their order.
	 *
	 * It acts as a hint only and it depends on the chooser whether this option has any effect.
	 *
	 * The default version always returns true. (randomise actions)
	 */
	virtual bool randomiseFixedActions(const BeliefNode* belief) const;

  private:
};

namespace ChooserDataBase_detail {

/** The real base class for ChooserDataBase.
 *
 * Do not implement this, but ChooserDataBase so serialisation works.
 */
class ChooserDataBaseBase {
	typedef ChooserDataBaseBase This;
	typedef ContinuousActionMap ThisActionMap;
public:
	virtual ~ChooserDataBaseBase() = default;
	_NO_COPY_OR_MOVE(ChooserDataBaseBase);


	virtual void saveToStream(const ThisActionMap& map, std::ostream& os) const = 0;
	static std::unique_ptr<This> loadFromStream(const ThisActionMap& map, std::istream& is);
protected:
	virtual void saveToStream_impl(const ThisActionMap& map, std::ostream& os) const = 0;

	typedef std::function<std::unique_ptr<ChooserDataBaseBase>(std::istream&)> LoadFromStreamFunction;
	static void registerDerivedType(const std::string& name, const LoadFromStreamFunction& loader);
private:
	static std::unordered_map<std::string, LoadFromStreamFunction>& getDerivedLoadersSingleton();
};

} // namespace ChooserDataBase_detail

/** A base class to hold data for the chooser.
 *
 * An implementation of this data structure can be stored in a continuous action map.
 * Its use it at the chooser's discresion.
 *
 * The action map will take care of serialisation and destruction.
 *
 * implementation are expected to be constructible from std::istream for de-serialisation.
 *
 */
template<class Derived>
class ChooserDataBase: public ChooserDataBase_detail::ChooserDataBaseBase {
	typedef ChooserDataBase This;
	typedef ChooserDataBase_detail::ChooserDataBaseBase Base;
public:
	virtual ~ChooserDataBase() = default;
	_NO_COPY_OR_MOVE(ChooserDataBase);

private:
	static bool initialisationDummy;
	static void registerType();
};

template<class Derived>
bool ChooserDataBase<Derived>::initialisationDummy = (ChooserDataBase<Derived>::registerType(), true);

template<class Derived>
inline void ChooserDataBase<Derived>::registerType() {
	registerDerivedType(typeid(Derived).name, [](std::istream& is) { std::make_unique<Derived>(is); });
}



/** A concrete class implementing ActionMapping for a continuous or hybrid action space.
 *
 * This class stores its mapping entries in an unordered_map for easy access. In addition
 * it allows the chooser to store and access additional data.
 */
class ContinuousActionMap: public solver::ActionMapping {
private:
	typedef ContinuousActionMap This;
	typedef ContinuousActionMapEntry ThisActionMapEntry;
	typedef ContinuousAction ThisAction;
	typedef ContinuousActionConstructionDataBase ThisActionConstructionData;
  public:
    friend class ContinuousActionMapEntry;
    friend class ContinuousActionTextSerializer;

    /** Constructs a new DiscretizedActionMap, which will be owned by the given belief node,
     * and be associated with the given DiscretizedActionpool.
     *
     */
    ContinuousActionMap(BeliefNode *owner, ContinuousActionPool *pool);

    // Default destructor; copying and moving disallowed!
    virtual ~ContinuousActionMap() = default;
    _NO_COPY_OR_MOVE(ContinuousActionMap);

    /* -------------- Retrieval internal infrastructure members ---------------- */
    const ContinuousActionPool* getActionPool() const;

    /* -------------- Creation and retrieval of nodes. ---------------- */
    virtual ActionNode *getActionNode(Action const &action) const override;
    virtual ActionNode *createActionNode(Action const &action) override;
    virtual long getNChildren() const override;

    // TODO: This is not const. double check the interface. I think this should be changed everywhere in tapir...
    virtual void deleteChild(ActionMappingEntry const *entry) override;

    /* -------------- Retrieval of mapping entries. ---------------- */
    virtual std::vector<ActionMappingEntry const *> getChildEntries() const override;

    virtual long getNumberOfVisitedEntries() const override;
    virtual std::vector<ActionMappingEntry const *> getVisitedEntries() const override;
    virtual ActionMappingEntry *getEntry(Action const &action) override;
    virtual ActionMappingEntry const *getEntry(Action const &action) const override;

    /* ----------------- Methods for unvisited actions ------------------- */
    /** Returns the next action to be tried for this node, or nullptr if there are no more. */
    virtual std::unique_ptr<Action> getNextActionToTry() override;

    /* -------------- Retrieval of general statistics. ---------------- */
    virtual long getTotalVisitCount() const override;



  protected:
    /** The pool associated with this mapping. */
    ContinuousActionPool *pool;

    /** The container to store the action map entries. */
    std::unique_ptr<ContinuousActionContainerBase> entries;

    /** The number of action node children that have been created. */
    long nChildren = 0;

    /** The number of entries with nonzero visit counts. */
    long numberOfVisitedEntries = 0;

    /** The total of the visit counts of all of the individual entries. */
    long totalVisitCount = 0;
};


/** A concrete class implementing ActionMappingEntry for a discretized action space.
 *
 * Each entry stores its bin number and a reference back to its parent map, as well as a child node,
 * visit count, total and mean Q-values, and a flag for whether or not the action is legal.
 */
class ContinuousActionMapEntry : public solver::ActionMappingEntry {

	typedef ContinuousActionMapEntry This;
	typedef ContinuousActionMap ThisActionMap;
	typedef ContinuousActionConstructionDataBase ThisActionConstructionData;


    friend class ContinuousActionMapTextSerializer;


  public:
    ContinuousActionMapEntry(ThisActionMap* map, std::unique_ptr<ThisActionConstructionData>&& constructionData, bool isLegal = false);
	_NO_COPY_OR_MOVE(ContinuousActionMapEntry);

    virtual ActionMapping *getMapping() const override;
    virtual std::unique_ptr<Action> getAction() const override;
    virtual ActionNode *getActionNode() const override;
    virtual long getVisitCount() const override;
    virtual double getTotalQValue() const override;
    virtual double getMeanQValue() const override;
    virtual bool isLegal() const override;

    /** Returns the bin number associated with this entry. */
    long getBinNumber() const;

    virtual bool update(long deltaNVisits, double deltaTotalQ) override;
    virtual void setLegal(bool legal) override;

    void setChild(std::unique_ptr<ActionNode>&& child);
    void deleteChild();
    const ActionNode* getChild() const;

  protected:
    /** The parent action mapping. */
    ContinuousActionMap* const map = nullptr;

    /** The construction data represented by this entry */
    std::unique_ptr<ThisActionConstructionData> constructionData;

    /** The child action node, if one exists. */
    std::unique_ptr<ActionNode> childNode = nullptr;
    /** The visit count for this edge. */
    long visitCount_ = 0;
    /** The total Q-value for this edge. */
    double totalQValue_ = 0;
    /** The mean Q-value for this edge => should be equal to totalQValue_ / visitCount_ */
    double meanQValue_ = 0;
    /** True iff this edge is legal. */
    bool isLegal_ = false; // Entries are illegal by default.
};

/** A partial implementation of the Serializer interface which provides serialization methods for
 * the above continuous action mapping classes.
 */
class ContinuousActionTextSerializer: virtual public solver::Serializer {
	typedef ContinuousActionTextSerializer This;
	typedef ContinuousActionMap ThisActionMap;
	typedef ContinuousActionMapEntry ThisActionMapEntry;
  public:
	ContinuousActionTextSerializer() = default;
    virtual ~ContinuousActionTextSerializer() = default;
    _NO_COPY_OR_MOVE(ContinuousActionTextSerializer);

    virtual void saveActionPool(ActionPool const &actionPool, std::ostream &os) override;
    virtual std::unique_ptr<ActionPool> loadActionPool(std::istream &is) override;
    virtual void saveActionMapping(ActionMapping const &map, std::ostream &os) override;
    virtual std::unique_ptr<ActionMapping> loadActionMapping(BeliefNode *node, std::istream &is) override;

    /** Loads the data from the input stream into the given ThisActionMap. */
    virtual void loadActionMapping(ThisActionMap &discMap, std::istream &is);
  protected:
    virtual void saveActionMapEntry(const ThisActionMapEntry& entry, std::ostream& os);
    virtual std::unique_ptr<ThisActionMapEntry> loadActionMapEntry(const ThisActionMapEntry& entry, std::istream& is);
};





/* ------------------- Template Implementations ------------------- */


template<class CONSTRUCTION_DATA>
inline std::unique_ptr<ContinuousActionMapEntry>& ContinuousActionContainer<CONSTRUCTION_DATA>::at(const ContinuousActionConstructionDataBase& key) {
	return container.at(static_cast<KeyType&>(key));
}

template<class CONSTRUCTION_DATA>
inline const std::unique_ptr<ContinuousActionMapEntry>& ContinuousActionContainer<CONSTRUCTION_DATA>::at(const ContinuousActionConstructionDataBase& key) const {
	return container.at(static_cast<KeyType&>(key));
}

template<class CONSTRUCTION_DATA>
inline std::unique_ptr<ContinuousActionMapEntry>& ContinuousActionContainer<CONSTRUCTION_DATA>::operator[](const ContinuousActionConstructionDataBase& key) {
	return container[static_cast<KeyType&>(key)];
}


template<class CONSTRUCTION_DATA>
inline std::vector<ActionMappingEntry const*> ContinuousActionContainer<CONSTRUCTION_DATA>::getEntriesWithChildren() const {
	std::vector<ActionMappingEntry const *> result;
	// let's assume most of them have children.
	result.reserve(container.size());
	for(auto& i : container) {
		ContinuousActionMapEntry const &entry = *(i.second);
		if (entry.getChild() != nullptr) {
			result.push_back(&entry);
		}
	}
	return std::move(result);
}

template<class CONSTRUCTION_DATA>
inline std::vector<ActionMappingEntry const*> ContinuousActionContainer<CONSTRUCTION_DATA>::getEntriesWithNonzeroVisitCount() const {
	std::vector<ActionMappingEntry const *> result;
	// let's assume most of them have been visited.
	result.reserve(container.size());
	for(auto& i : container) {
		ContinuousActionMapEntry const &entry = *(i.second);
		if (entry.getVisitCount() > 0) {
			if (!entry.isLegal()) {
				debug::show_message("WARNING: Illegal entry with nonzero visit count!");
			}
			result.push_back(&entry);
		}
	}
	return std::move(result);
}






} /* namespace solver */

#endif /* SOLVER_CONTINUOUS_ACTIONS_HPP_ */