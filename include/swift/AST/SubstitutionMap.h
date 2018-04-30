//===--- SubstitutionMap.h - Swift Substitution Map ASTs --------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines the SubstitutionMap class.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_SUBSTITUTION_MAP_H
#define SWIFT_AST_SUBSTITUTION_MAP_H

#include "swift/AST/GenericSignature.h"
#include "swift/AST/ProtocolConformanceRef.h"
#include "swift/AST/SubstitutionList.h"
#include "swift/AST/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/TrailingObjects.h"

namespace llvm {
  class FoldingSetNodeID;
}

namespace swift {

class GenericEnvironment;
class SubstitutableType;
typedef CanTypeWrapper<GenericTypeParamType> CanGenericTypeParamType;

template<class Type> class CanTypeWrapper;
typedef CanTypeWrapper<SubstitutableType> CanSubstitutableType;

enum class CombineSubstitutionMaps {
  AtDepth,
  AtIndex
};

/// SubstitutionMap is a data structure type that describes the mapping of
/// abstract types to replacement types, together with associated conformances
/// to use for deriving nested types and conformances.
///
/// Depending on how the SubstitutionMap is constructed, the abstract types are
/// either archetypes or interface types. Care must be exercised to only look
/// up one or the other.
///
/// SubstitutionMaps are constructed by calling the getSubstitutionMap() method
/// on a GenericSignature or (equivalently) by calling one of the static
/// \c SubstitutionMap::get() methods.
class SubstitutionMap {
public:
  /// Stored data for a substitution map, which uses tail allocation for the
  /// replacement types and conformances.
  class Storage final
    : public llvm::FoldingSetNode,
      llvm::TrailingObjects<Storage, Type, ProtocolConformanceRef>
  {
    friend TrailingObjects;

    /// The generic signature for which we are performing substitutions.
    GenericSignature * const genericSig;

    /// The number of conformance requirements, cached to avoid constantly
    /// recomputing it on conformance-buffer access.
    const unsigned numConformanceRequirements;

    Storage() = delete;

    Storage(GenericSignature *genericSig,
            ArrayRef<Type> replacementTypes,
            ArrayRef<ProtocolConformanceRef> conformances);

  private:
    unsigned getNumReplacementTypes() const {
      return genericSig->getGenericParams().size();
    }

    size_t numTrailingObjects(OverloadToken<Type>) const {
      return getNumReplacementTypes();
    }

    size_t numTrailingObjects(OverloadToken<ProtocolConformanceRef>) const {
      return numConformanceRequirements;
    }

  public:
    /// Form storage for the given generic signature and its replacement
    /// types and conformances.
    static Storage *get(GenericSignature *genericSig,
                        ArrayRef<Type> replacementTypes,
                        ArrayRef<ProtocolConformanceRef> conformances);

    /// Retrieve the generic signature that describes the shape of this
    /// storage.
    GenericSignature *getGenericSignature() const { return genericSig; }

    /// Retrieve the array of replacement types, which line up with the
    /// generic parameters.
    ///
    /// Note that the types may be null, for cases where the generic parameter
    /// is concrete but hasn't been queried yet.
    ArrayRef<Type> getReplacementTypes() const {
      return llvm::makeArrayRef(getTrailingObjects<Type>(),
                                getNumReplacementTypes());
    }

    MutableArrayRef<Type> getReplacementTypes() {
      return MutableArrayRef<Type>(getTrailingObjects<Type>(),
                                   getNumReplacementTypes());
    }

    /// Retrieve the array of protocol conformances, which line up with the
    /// requirements of the generic signature.
    ArrayRef<ProtocolConformanceRef> getConformances() const {
      return llvm::makeArrayRef(getTrailingObjects<ProtocolConformanceRef>(),
                                numConformanceRequirements);
    }
    MutableArrayRef<ProtocolConformanceRef> getConformances() {
      return MutableArrayRef<ProtocolConformanceRef>(
                                getTrailingObjects<ProtocolConformanceRef>(),
                                numConformanceRequirements);
    }

    /// Profile the substitution map storage, for use with LLVM's FoldingSet.
    void Profile(llvm::FoldingSetNodeID &id) const {
      Profile(id, getGenericSignature(), getReplacementTypes(),
              getConformances());
    }

    /// Profile the substitution map storage, for use with LLVM's FoldingSet.
    static void Profile(llvm::FoldingSetNodeID &id,
                        GenericSignature *genericSig,
                        ArrayRef<Type> replacementTypes,
                        ArrayRef<ProtocolConformanceRef> conformances);
  };

private:
  /// The storage needed to describe the set of substitutions.
  ///
  /// When null, this substitution map is empty, having neither a generic
  /// signature nor any replacement types/conformances.
  Storage *storage = nullptr;

  /// Retrieve the array of replacement types, which line up with the
  /// generic parameters.
  ///
  /// Note that the types may be null, for cases where the generic parameter
  /// is concrete but hasn't been queried yet.
  ArrayRef<Type> getReplacementTypes() const {
    return storage ? storage->getReplacementTypes() : ArrayRef<Type>();
  }

  MutableArrayRef<Type> getReplacementTypes() {
    return storage ? storage->getReplacementTypes() : MutableArrayRef<Type>();
  }

  /// Retrieve the array of protocol conformances, which line up with the
  /// requirements of the generic signature.
  ArrayRef<ProtocolConformanceRef> getConformances() const {
    return storage ? storage->getConformances()
                   : ArrayRef<ProtocolConformanceRef>();
  }
  MutableArrayRef<ProtocolConformanceRef> getConformances() {
    return storage ? storage->getConformances()
                   : MutableArrayRef<ProtocolConformanceRef>();
  }

  /// Form a substitution map for the given generic signature with the
  /// specified replacement types and conformances.
  SubstitutionMap(GenericSignature *genericSig,
                  ArrayRef<Type> replacementTypes,
                  ArrayRef<ProtocolConformanceRef> conformances)
    : storage(Storage::get(genericSig, replacementTypes, conformances)) { }

public:
  /// Build an empty substitution map.
  SubstitutionMap() { }

  /// Build an interface type substitution map for the given generic
  /// signature and a vector of Substitutions that correspond to the
  /// requirements of this generic signature.
  static SubstitutionMap get(GenericSignature *genericSig,
                             SubstitutionList substitutions);

  /// Build an interface type substitution map for the given generic signature
  /// from a type substitution function and conformance lookup function.
  static SubstitutionMap get(GenericSignature *genericSig,
                             TypeSubstitutionFn subs,
                             LookupConformanceFn lookupConformance);

  /// Retrieve the generic signature describing the environment in which
  /// substitutions occur.
  GenericSignature *getGenericSignature() const {
    return storage ? storage->getGenericSignature() : nullptr;
  }

  /// Look up a conformance for the given type to the given protocol.
  Optional<ProtocolConformanceRef>
  lookupConformance(CanType type, ProtocolDecl *proto) const;

  /// Whether the substitution map is empty.
  bool empty() const { return getGenericSignature() == nullptr; }

  /// Query whether any replacement types in the map contain archetypes.
  bool hasArchetypes() const;

  /// Query whether any replacement types in the map contain an opened
  /// existential.
  bool hasOpenedExistential() const;

  /// Query whether any replacement type sin the map contain dynamic Self.
  bool hasDynamicSelf() const;

  /// Apply a substitution to all replacement types in the map. Does not
  /// change keys.
  SubstitutionMap subst(const SubstitutionMap &subMap) const;

  /// Apply a substitution to all replacement types in the map. Does not
  /// change keys.
  SubstitutionMap subst(TypeSubstitutionFn subs,
                        LookupConformanceFn conformances) const;

  /// Create a substitution map for a protocol conformance.
  static SubstitutionMap
  getProtocolSubstitutions(ProtocolDecl *protocol,
                           Type selfType,
                           ProtocolConformanceRef conformance);

  /// Given that 'derivedDecl' is an override of 'baseDecl' in a subclass,
  /// and 'derivedSubs' is a set of substitutions written in terms of the
  /// generic signature of 'derivedDecl', produce a set of substitutions
  /// written in terms of the generic signature of 'baseDecl'.
  static SubstitutionMap
  getOverrideSubstitutions(const ValueDecl *baseDecl,
                           const ValueDecl *derivedDecl,
                           Optional<SubstitutionMap> derivedSubs);

  /// Variant of the above for when we have the generic signatures but not
  /// the decls for 'derived' and 'base'.
  static SubstitutionMap
  getOverrideSubstitutions(const ClassDecl *baseClass,
                           const ClassDecl *derivedClass,
                           GenericSignature *baseSig,
                           GenericSignature *derivedSig,
                           Optional<SubstitutionMap> derivedSubs);

  /// Combine two substitution maps as follows.
  ///
  /// The result is written in terms of the generic parameters of 'genericSig'.
  ///
  /// Generic parameters with a depth or index less than 'firstDepthOrIndex'
  /// come from 'firstSubMap'.
  ///
  /// Generic parameters with a depth greater than 'firstDepthOrIndex' come
  /// from 'secondSubMap', but are looked up starting with a depth or index of
  /// 'secondDepthOrIndex'.
  ///
  /// The 'how' parameter determines if we're looking at the depth or index.
  static SubstitutionMap
  combineSubstitutionMaps(const SubstitutionMap &firstSubMap,
                          const SubstitutionMap &secondSubMap,
                          CombineSubstitutionMaps how,
                          unsigned baseDepthOrIndex,
                          unsigned origDepthOrIndex,
                          GenericSignature *genericSig);

  /// Swap archetypes in the substitution map's replacement types with their
  /// interface types.
  SubstitutionMap mapReplacementTypesOutOfContext() const;

  /// Verify that this substitution map is valid.
  void verify() const;

  /// Dump the contents of this substitution map for debugging purposes.
  void dump(llvm::raw_ostream &out) const;

  LLVM_ATTRIBUTE_DEPRECATED(void dump() const, "only for use in the debugger");

  /// Profile the substitution map, for use with LLVM's FoldingSet.
  void profile(llvm::FoldingSetNodeID &id) const;

private:
  friend class GenericSignature;
  friend class GenericEnvironment;
  friend struct QuerySubstitutionMap;

  /// Look up the replacement for the given type parameter or interface type.
  /// Note that this only finds replacements for maps that are directly
  /// stored inside the map. In most cases, you should call Type::subst()
  /// instead, since that will resolve member types also.
  Type lookupSubstitution(CanSubstitutableType type) const;
};

} // end namespace swift

#endif
