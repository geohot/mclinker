//===- MipsGOT.h ----------------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef MCLD_MIPS_GOT_H
#define MCLD_MIPS_GOT_H
#include <list>
#include <map>

#ifdef ENABLE_UNITTEST
#include <gtest.h>
#endif

#include <llvm/ADT/DenseMap.h>

#include <mcld/Target/GOT.h>

namespace mcld
{
class Input;
class LDSection;
class MemoryRegion;

/** \class MipsGOTEntry
 *  \brief GOT Entry with size of 4 bytes
 */
class MipsGOTEntry : public GOT::Entry<4>
{
public:
  MipsGOTEntry(uint64_t pContent, SectionData* pParent)
   : GOT::Entry<4>(pContent, pParent)
  {}
};

/** \class MipsGOT
 *  \brief Mips Global Offset Table.
 */
class MipsGOT : public GOT
{
public:
  MipsGOT(LDSection& pSection);

  uint64_t emit(MemoryRegion& pRegion);

  void reserveLocalEntry(const Input& pInput);
  void reserveGlobalEntry(const Input& pInput, const ResolveInfo& pInfo);

  size_t getTotalNum() const;
  size_t getLocalNum() const;

  MipsGOTEntry* consumeLocal();
  MipsGOTEntry* consumeGlobal();

  void setLocal(const ResolveInfo* pInfo) {
    m_GOTTypeMap[pInfo] = false;
  }

  void setGlobal(const ResolveInfo* pInfo) {
    m_GOTTypeMap[pInfo] = true;
  }

  bool isLocal(const ResolveInfo* pInfo) {
    return m_GOTTypeMap[pInfo] == false;
  }

  bool isGlobal(const ResolveInfo* pInfo) {
    return m_GOTTypeMap[pInfo] == true;
  }

  /// hasGOT1 - return if this got section has any GOT1 entry
  bool hasGOT1() const;

public:
  /// Do real allocation of the GOT entries.
  virtual void finalizeSectionSize();

private:
  /** \class GOTMultipart
   *  \brief GOTMultipart counts local and global entries in the GOT.
   */
  struct GOTMultipart
  {
    GOTMultipart(size_t local = 0, size_t global = 0);

    size_t m_LocalNum;  ///< number of reserved local entries
    size_t m_GlobalNum; ///< number of reserved global entries
  };

  typedef std::list<GOTMultipart> MultipartListType;
  typedef std::set<const ResolveInfo*> SymbolSetType;

  MultipartListType m_MultipartList;  ///< list of GOT's descriptors
  GOTMultipart m_CurrentGOT;          ///< number of GOT entries from current input
  const Input* m_pInput;              ///< current input
  SymbolSetType m_MergedGlobalSymbols;///< merged global symbols
  SymbolSetType m_InputGlobalSymbols; ///< global symbols from current input

  void merge(const Input& pInput);

private:
  typedef llvm::DenseMap<const ResolveInfo*, bool> SymbolTypeMapType;

  SymbolTypeMapType m_GOTTypeMap;

  size_t m_LocalNum;            ///< number of reserved local entries
  size_t m_GlobalNum;           ///< number of reserved global entries

  MipsGOTEntry* m_pLastLocal;   ///< the last consumed local entry
  MipsGOTEntry* m_pLastGlobal;  ///< the last consumed global entry

private:
  void reserve(size_t pNum);
};

} // namespace of mcld

#endif

