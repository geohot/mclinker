//===- FragmentLinker.cpp -------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the FragmentLinker class
//
//===----------------------------------------------------------------------===//
#include <mcld/Fragment/FragmentLinker.h>

#include <llvm/Support/Host.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Casting.h>

#include <mcld/LinkerConfig.h>
#include <mcld/Module.h>
#include <mcld/MC/MCLDInput.h>
#include <mcld/LD/BranchIslandFactory.h>
#include <mcld/LD/Resolver.h>
#include <mcld/LD/LDContext.h>
#include <mcld/LD/RelocationFactory.h>
#include <mcld/LD/RelocationData.h>
#include <mcld/LD/SectionRules.h>
#include <mcld/Support/MemoryRegion.h>
#include <mcld/Support/FileHandle.h>
#include <mcld/Support/MsgHandling.h>
#include <mcld/Target/TargetLDBackend.h>

using namespace mcld;

/// Constructor
FragmentLinker::FragmentLinker(const LinkerConfig& pConfig,
                               Module& pModule,
                               TargetLDBackend& pBackend)

  : m_Config(pConfig),
    m_Module(pModule),
    m_Backend(pBackend),
    m_pSectionRules(NULL)
{
}

/// Destructor
FragmentLinker::~FragmentLinker()
{
  delete m_pSectionRules;
}

//===----------------------------------------------------------------------===//
// Symbol Operations
//===----------------------------------------------------------------------===//
/// addSymbolFromObject - add a symbol from object file and resolve it
/// immediately
LDSymbol* FragmentLinker::addSymbolFromObject(const llvm::StringRef& pName,
                                        ResolveInfo::Type pType,
                                        ResolveInfo::Desc pDesc,
                                        ResolveInfo::Binding pBinding,
                                        ResolveInfo::SizeType pSize,
                                        LDSymbol::ValueType pValue,
                                        FragmentRef* pFragmentRef,
                                        ResolveInfo::Visibility pVisibility)
{

  // Step 1. calculate a Resolver::Result
  // resolved_result is a triple <resolved_info, existent, override>
  Resolver::Result resolved_result;
  ResolveInfo old_info; // used for arrange output symbols

  if (pBinding == ResolveInfo::Local) {
    // if the symbol is a local symbol, create a LDSymbol for input, but do not
    // resolve them.
    resolved_result.info     = m_Module.getNamePool().createSymbol(pName,
                                                                   false,
                                                                   pType,
                                                                   pDesc,
                                                                   pBinding,
                                                                   pSize,
                                                                   pVisibility);

    // No matter if there is a symbol with the same name, insert the symbol
    // into output symbol table. So, we let the existent false.
    resolved_result.existent  = false;
    resolved_result.overriden = true;
  }
  else {
    // if the symbol is not local, insert and resolve it immediately
    m_Module.getNamePool().insertSymbol(pName, false, pType, pDesc, pBinding,
                                        pSize, pVisibility,
                                        &old_info, resolved_result);
  }

  // the return ResolveInfo should not NULL
  assert(NULL != resolved_result.info);

  /// Step 2. create an input LDSymbol.
  // create a LDSymbol for the input file.
  LDSymbol* input_sym = LDSymbol::Create(*resolved_result.info);
  input_sym->setFragmentRef(pFragmentRef);
  input_sym->setValue(pValue);

  // Step 3. Set up corresponding output LDSymbol
  LDSymbol* output_sym = resolved_result.info->outSymbol();
  bool has_output_sym = (NULL != output_sym);
  if (!resolved_result.existent || !has_output_sym) {
    // it is a new symbol, the output_sym should be NULL.
    assert(NULL == output_sym);

    // if it is a new symbol, create a LDSymbol for the output
    output_sym = LDSymbol::Create(*resolved_result.info);
    resolved_result.info->setSymPtr(output_sym);
  }

  if (resolved_result.overriden || !has_output_sym) {
    // symbol can be overriden only if it exists.
    assert(output_sym != NULL);

    // should override output LDSymbol
    output_sym->setFragmentRef(pFragmentRef);
    output_sym->setValue(pValue);
  }

  // Step 4. Adjust the position of output LDSymbol.
  // After symbol resolution, visibility is changed to the most restrict one.
  // we need to arrange its position in the output symbol. We arrange the
  // positions by sorting symbols in SymbolCategory.
  if (pType != ResolveInfo::Section) {
    if (!has_output_sym) {
      // We merge sections when reading them. So we do not need to output symbols
      // with section type

      // No matter the symbol is already in the output or not, add it if it
      // should be forcefully set local.
      if (shouldForceLocal(*resolved_result.info))
        m_Module.getSymbolTable().forceLocal(*output_sym);
      else {
        // the symbol should not be forcefully local.
        m_Module.getSymbolTable().add(*output_sym);
      }
    }
    else if (resolved_result.overriden) {
      if (!shouldForceLocal(old_info) ||
          !shouldForceLocal(*resolved_result.info)) {
        // If the old info and the new info are both forcefully local, then
        // we should keep the output_sym in forcefully local category. Else,
        // we should re-sort the output_sym
        m_Module.getSymbolTable().arrange(*output_sym, old_info);
      }
    }
  }

  return input_sym;
}

/// addSymbolFromDynObj - add a symbol from object file and resolve it
/// immediately
LDSymbol* FragmentLinker::addSymbolFromDynObj(const llvm::StringRef& pName,
                                        ResolveInfo::Type pType,
                                        ResolveInfo::Desc pDesc,
                                        ResolveInfo::Binding pBinding,
                                        ResolveInfo::SizeType pSize,
                                        LDSymbol::ValueType pValue,
                                        FragmentRef* pFragmentRef,
                                        ResolveInfo::Visibility pVisibility)
{
  // We don't need sections of dynamic objects. So we ignore section symbols.
  if (pType == ResolveInfo::Section)
    return NULL;

  // ignore symbols with local binding or that have internal or hidden
  // visibility
  if (pBinding == ResolveInfo::Local ||
      pVisibility == ResolveInfo::Internal ||
      pVisibility == ResolveInfo::Hidden)
    return NULL;

  // A protected symbol in a shared library must be treated as a
  // normal symbol when viewed from outside the shared library.
  if (pVisibility == ResolveInfo::Protected)
    pVisibility = ResolveInfo::Default;

  // insert symbol and resolve it immediately
  // resolved_result is a triple <resolved_info, existent, override>
  Resolver::Result resolved_result;
  m_Module.getNamePool().insertSymbol(pName, true, pType, pDesc,
                                      pBinding, pSize, pVisibility,
                                      NULL, resolved_result);

  // the return ResolveInfo should not NULL
  assert(NULL != resolved_result.info);

  // create a LDSymbol for the input file.
  LDSymbol* input_sym = LDSymbol::Create(*resolved_result.info);
  input_sym->setFragmentRef(pFragmentRef);
  input_sym->setValue(pValue);

  LDSymbol* output_sym = NULL;
  if (!resolved_result.existent) {
    // we get a new symbol, leave it as NULL
    resolved_result.info->setSymPtr(NULL);
  }
  else {
    // we saw the symbol before, but the output_sym still may be NULL.
    output_sym = resolved_result.info->outSymbol();
  }

  if (output_sym != NULL) {
    // After symbol resolution, visibility is changed to the most restrict one.
    // If we are not doing incremental linking, then any symbol with hidden
    // or internal visibility is forcefully set as a local symbol.
    if (shouldForceLocal(*resolved_result.info)) {
      m_Module.getSymbolTable().forceLocal(*output_sym);
    }
  }

  return input_sym;
}

/// defineSymbolForcefully - define an output symbol and override it immediately
LDSymbol* FragmentLinker::defineSymbolForcefully(const llvm::StringRef& pName,
                                           bool pIsDyn,
                                           ResolveInfo::Type pType,
                                           ResolveInfo::Desc pDesc,
                                           ResolveInfo::Binding pBinding,
                                           ResolveInfo::SizeType pSize,
                                           LDSymbol::ValueType pValue,
                                           FragmentRef* pFragmentRef,
                                           ResolveInfo::Visibility pVisibility)
{
  ResolveInfo* info = m_Module.getNamePool().findInfo(pName);
  LDSymbol* output_sym = NULL;
  if (NULL == info) {
    // the symbol is not in the pool, create a new one.
    // create a ResolveInfo
    Resolver::Result result;
    m_Module.getNamePool().insertSymbol(pName, pIsDyn, pType, pDesc,
                                        pBinding, pSize, pVisibility,
                                        NULL, result);
    assert(!result.existent);

    // create a output LDSymbol
    output_sym = LDSymbol::Create(*result.info);
    result.info->setSymPtr(output_sym);

    if (shouldForceLocal(*result.info))
      m_Module.getSymbolTable().forceLocal(*output_sym);
    else
      m_Module.getSymbolTable().add(*output_sym);
  }
  else {
    // the symbol is already in the pool, override it
    ResolveInfo old_info;
    old_info.override(*info);

    info->setSource(pIsDyn);
    info->setType(pType);
    info->setDesc(pDesc);
    info->setBinding(pBinding);
    info->setVisibility(pVisibility);
    info->setIsSymbol(true);
    info->setSize(pSize);

    output_sym = info->outSymbol();
    if (NULL != output_sym)
      m_Module.getSymbolTable().arrange(*output_sym, old_info);
    else {
      // create a output LDSymbol
      output_sym = LDSymbol::Create(*info);
      info->setSymPtr(output_sym);

      m_Module.getSymbolTable().add(*output_sym);
    }
  }

  if (NULL != output_sym) {
    output_sym->setFragmentRef(pFragmentRef);
    output_sym->setValue(pValue);
  }

  return output_sym;
}

/// defineSymbolAsRefered - define an output symbol and override it immediately
LDSymbol* FragmentLinker::defineSymbolAsRefered(const llvm::StringRef& pName,
                                           bool pIsDyn,
                                           ResolveInfo::Type pType,
                                           ResolveInfo::Desc pDesc,
                                           ResolveInfo::Binding pBinding,
                                           ResolveInfo::SizeType pSize,
                                           LDSymbol::ValueType pValue,
                                           FragmentRef* pFragmentRef,
                                           ResolveInfo::Visibility pVisibility)
{
  ResolveInfo* info = m_Module.getNamePool().findInfo(pName);

  if (NULL == info || !(info->isUndef() || info->isDyn())) {
    // only undefined symbol and dynamic symbol can make a reference.
    return NULL;
  }

  // the symbol is already in the pool, override it
  ResolveInfo old_info;
  old_info.override(*info);

  info->setSource(pIsDyn);
  info->setType(pType);
  info->setDesc(pDesc);
  info->setBinding(pBinding);
  info->setVisibility(pVisibility);
  info->setIsSymbol(true);
  info->setSize(pSize);

  LDSymbol* output_sym = info->outSymbol();
  if (NULL != output_sym) {
    output_sym->setFragmentRef(pFragmentRef);
    output_sym->setValue(pValue);
    m_Module.getSymbolTable().arrange(*output_sym, old_info);
  }
  else {
    // create a output LDSymbol
    output_sym = LDSymbol::Create(*info);
    info->setSymPtr(output_sym);

    m_Module.getSymbolTable().add(*output_sym);
  }

  return output_sym;
}

/// defineAndResolveSymbolForcefully - define an output symbol and resolve it
/// immediately
LDSymbol* FragmentLinker::defineAndResolveSymbolForcefully(const llvm::StringRef& pName,
                                                     bool pIsDyn,
                                                     ResolveInfo::Type pType,
                                                     ResolveInfo::Desc pDesc,
                                                     ResolveInfo::Binding pBinding,
                                                     ResolveInfo::SizeType pSize,
                                                     LDSymbol::ValueType pValue,
                                                     FragmentRef* pFragmentRef,
                                                     ResolveInfo::Visibility pVisibility)
{
  // Result is <info, existent, override>
  Resolver::Result result;
  ResolveInfo old_info;
  m_Module.getNamePool().insertSymbol(pName, pIsDyn, pType, pDesc, pBinding,
                                      pSize, pVisibility,
                                      &old_info, result);

  LDSymbol* output_sym = result.info->outSymbol();
  bool has_output_sym = (NULL != output_sym);

  if (!result.existent || !has_output_sym) {
    output_sym = LDSymbol::Create(*result.info);
    result.info->setSymPtr(output_sym);
  }

  if (result.overriden || !has_output_sym) {
    output_sym->setFragmentRef(pFragmentRef);
    output_sym->setValue(pValue);
  }

  // After symbol resolution, the visibility is changed to the most restrict.
  // arrange the output position
  if (shouldForceLocal(*result.info))
    m_Module.getSymbolTable().forceLocal(*output_sym);
  else if (has_output_sym)
    m_Module.getSymbolTable().arrange(*output_sym, old_info);
  else
    m_Module.getSymbolTable().add(*output_sym);

  return output_sym;
}

/// defineAndResolveSymbolAsRefered - define an output symbol and resolve it
/// immediately.
LDSymbol* FragmentLinker::defineAndResolveSymbolAsRefered(const llvm::StringRef& pName,
                                                    bool pIsDyn,
                                                    ResolveInfo::Type pType,
                                                    ResolveInfo::Desc pDesc,
                                                    ResolveInfo::Binding pBinding,
                                                    ResolveInfo::SizeType pSize,
                                                    LDSymbol::ValueType pValue,
                                                    FragmentRef* pFragmentRef,
                                                    ResolveInfo::Visibility pVisibility)
{
  ResolveInfo* info = m_Module.getNamePool().findInfo(pName);

  if (NULL == info || !(info->isUndef() || info->isDyn())) {
    // only undefined symbol and dynamic symbol can make a reference.
    return NULL;
  }

  return defineAndResolveSymbolForcefully(pName,
                                          pIsDyn,
                                          pType,
                                          pDesc,
                                          pBinding,
                                          pSize,
                                          pValue,
                                          pFragmentRef,
                                          pVisibility);
}

bool FragmentLinker::finalizeSymbols()
{
  Module::sym_iterator symbol, symEnd = m_Module.sym_end();
  for (symbol = m_Module.sym_begin(); symbol != symEnd; ++symbol) {

    if ((*symbol)->resolveInfo()->isAbsolute() ||
        (*symbol)->resolveInfo()->type() == ResolveInfo::File) {
      // absolute symbols or symbols with function type should have
      // zero value
      (*symbol)->setValue(0x0);
      continue;
    }

    if ((*symbol)->resolveInfo()->type() == ResolveInfo::ThreadLocal) {
      m_Backend.finalizeTLSSymbol(*this, **symbol);
      continue;
    }

    if ((*symbol)->hasFragRef()) {
      // set the virtual address of the symbol. If the output file is
      // relocatable object file, the section's virtual address becomes zero.
      // And the symbol's value become section relative offset.
      uint64_t value = (*symbol)->fragRef()->getOutputOffset();
      assert(NULL != (*symbol)->fragRef()->frag());
      uint64_t addr  = getLayout().getOutputLDSection(*(*symbol)->fragRef()->frag())->addr();
      (*symbol)->setValue(value + addr);
      continue;
    }
  }

  // finialize target-dependent symbols
  return m_Backend.finalizeSymbols(*this);
}

bool FragmentLinker::shouldForceLocal(const ResolveInfo& pInfo) const
{
  // forced local symbol matches all rules:
  // 1. We are not doing incremental linking.
  // 2. The symbol is with Hidden or Internal visibility.
  // 3. The symbol should be global or weak. Otherwise, local symbol is local.
  // 4. The symbol is defined or common
  if (LinkerConfig::Object != m_Config.codeGenType() &&
      (pInfo.visibility() == ResolveInfo::Hidden ||
         pInfo.visibility() == ResolveInfo::Internal) &&
      (pInfo.isGlobal() || pInfo.isWeak()) &&
      (pInfo.isDefine() || pInfo.isCommon()))
    return true;
  return false;
}

//===----------------------------------------------------------------------===//
// Section Operations
//===----------------------------------------------------------------------===//
/// createSectHdr - create the input section header
LDSection& FragmentLinker::createSectHdr(const std::string& pName,
                                   LDFileFormat::Kind pKind,
                                   uint32_t pType,
                                   uint32_t pFlag)
{
  // for user such as reader, standard/target fromat
  LDSection* result = LDSection::Create(pName, pKind, pType, pFlag);

  // try to get one from output LDSection
  LDSection* output_sect = m_pSectionRules->getMatchedSection(pName);
  if (NULL == output_sect) {
    const SectionMap::NamePair& pair = m_Config.scripts().sectionMap().find(pName);
    std::string output_name = (pair.isNull())?pName:pair.to;
    output_sect = LDSection::Create(output_name, pKind, pType, pFlag);
    m_Module.getSectionTable().push_back(output_sect);
  }

  return *result;
}

/// getOrCreateOutputSectHdr - for reader and standard/target format to get
/// or create the output's section header
LDSection& FragmentLinker::getOrCreateOutputSectHdr(const std::string& pName,
                                              LDFileFormat::Kind pKind,
                                              uint32_t pType,
                                              uint32_t pFlag,
                                              uint32_t pAlign)
{
  // try to get one from output LDSection
  LDSection* output_sect = m_pSectionRules->getMatchedSection(pName);
  if (NULL == output_sect) {
    const SectionMap::NamePair& pair = m_Config.scripts().sectionMap().find(pName);
    std::string output_name = (pair.isNull())?pName:pair.to;
    output_sect = LDSection::Create(output_name, pKind, pType, pFlag);
    output_sect->setAlign(pAlign);
    m_Module.getSectionTable().push_back(output_sect);

    switch (pKind) {
    case LDFileFormat::Regular:
    case LDFileFormat::BSS:
    case LDFileFormat::Debug:
    case LDFileFormat::GCCExceptTable:
    case LDFileFormat::Version:
    case LDFileFormat::Target: {
      m_pSectionRules->append(pName, *output_sect);
      break;
    }
    case LDFileFormat::Relocation: {
      if (LinkerConfig::Object == m_Config.codeGenType()) {
        m_pSectionRules->append(pName, *output_sect);
        break;
      }
    }
    case LDFileFormat::Null:
    case LDFileFormat::NamePool:
    case LDFileFormat::EhFrame:
    case LDFileFormat::EhFrameHdr:
    case LDFileFormat::Note:
    case LDFileFormat::Group:
    case LDFileFormat::MetaData:
    case LDFileFormat::Ignore:
    default:
      // do not append rule
      break;
    } // end of switch
  }

  return *output_sect;
}

/// getOrCreateInputSectData - get or create SectionData
/// pSection is input LDSection
SectionData&
FragmentLinker::getOrCreateInputSectData(LDSection& pSection)
{
  // if there is already a section data pointed by section, return it.
  SectionData* sect_data = pSection.getSectionData();

  // try to get one from output LDSection
  LDSection* output_sect =
    m_pSectionRules->getMatchedSection(pSection.name());

  assert(NULL != output_sect);

  sect_data = output_sect->getSectionData();

  if (NULL != sect_data) {
    pSection.setSectionData(sect_data);
    m_Layout.addInputRange(*sect_data, pSection);
    return *sect_data;
  }

  // if the output LDSection also has no SectionData, then create one.
  sect_data = SectionData::Create(*output_sect);
  pSection.setSectionData(sect_data);
  output_sect->setSectionData(sect_data);
  m_Layout.addInputRange(*sect_data, pSection);
  return *sect_data;
}

/// getOrCreateOutputSectData - get or create SectionData
/// pSection is output LDSection
SectionData&
FragmentLinker::getOrCreateOutputSectData(LDSection& pSection)
{
  SectionData* sect_data = NULL;
  if (!pSection.hasSectionData()) {
    sect_data = SectionData::Create(pSection);
    pSection.setSectionData(sect_data);
  }
  else
    sect_data = pSection.getSectionData();

  m_Layout.addInputRange(*sect_data, pSection);
  return *sect_data;
}


RelocationData& FragmentLinker::getOrCreateInputRelocData(LDSection& pSection)
{
  // if there is already a relocation data pointed by section, return it
  RelocationData* reloc_data = pSection.getRelocationData();
  if (NULL != reloc_data) {
    return *reloc_data;
  }
  // otherwise, create one and push it into Module's RelocDataTable
  reloc_data = RelocationData::Create(pSection);
  pSection.setRelocationData(reloc_data);
  m_Module.getRelocationDataTable().push_back(reloc_data);
  return *reloc_data;
}

RelocationData& FragmentLinker::getOrCreateOutputRelocData(LDSection& pSection)
{
  // if there is already a relocation data pointed by section, return it
  RelocationData* reloc_data = pSection.getRelocationData();
  if (NULL != reloc_data) {
    return *reloc_data;
  }
  // otherwise, create one
  reloc_data = RelocationData::Create(pSection);
  pSection.setRelocationData(reloc_data);
  return *reloc_data;
}

void FragmentLinker::initSectionMap()
{
  if (NULL == m_pSectionRules) {
    m_pSectionRules = new SectionRules(m_Config, m_Module);
    m_pSectionRules->initOutputSectMap();
  }
}

bool FragmentLinker::layout()
{
  return m_Layout.layout(m_Module, m_Backend, m_Config);
}

//===----------------------------------------------------------------------===//
// Relocation Operations
//===----------------------------------------------------------------------===//
/// addRelocation - add a relocation entry in FragmentLinker (only for object file)
///
/// All symbols should be read and resolved before calling this function.
Relocation* FragmentLinker::addRelocation(Relocation::Type pType,
                                          const LDSymbol& pSym,
                                          ResolveInfo& pResolveInfo,
                                          FragmentRef& pFragmentRef,
                                          LDSection& pSection,
                                          const LDSection& pTargetSection,
                                          Relocation::Address pAddend)
{
  // FIXME: we should dicard sections and symbols first instead
  // if the symbol is in the discarded input section, then we also need to
  // discard this relocation.
  if (pSym.fragRef() == NULL &&
      pResolveInfo.type() == ResolveInfo::Section &&
      pResolveInfo.desc() == ResolveInfo::Undefined)
    return NULL;

  Relocation* relocation = m_Backend.getRelocFactory()->produce(pType,
                                                                pFragmentRef,
                                                                pAddend);
  relocation->setSymInfo(&pResolveInfo);

  // push relocation into the input RelocationData
  RelocationData* reloc_data = NULL;
  reloc_data = &getOrCreateInputRelocData(pSection);
  reloc_data->getFragmentList().push_back(relocation);

  // scan relocation
  if (LinkerConfig::Object != m_Config.codeGenType())
    m_Backend.scanRelocation(*relocation, pSym, *this, pTargetSection);
  else
    m_Backend.partialScanRelocation(*relocation, pSym, *this, m_Module, pTargetSection);
  return relocation;
}

bool FragmentLinker::applyRelocations()
{
  // when producing relocatables, no need to apply relocation
  if (LinkerConfig::Object == m_Config.codeGenType())
    return true;

  // apply relocations from inputs
  Module::reloc_data_iterator dataIter, dataEnd = m_Module.reloc_data_end();
  for (dataIter = m_Module.reloc_data_begin(); dataIter != dataEnd; ++dataIter) {
    RelocationData* reloc_data = *dataIter;
    RelocationData::iterator relocIter, reloc_end = reloc_data->end();

    for (relocIter = reloc_data->begin(); relocIter != reloc_end; ++relocIter) {
      Relocation* reloc = llvm::cast<Relocation>(relocIter);
      reloc->apply(*m_Backend.getRelocFactory());
    }
  }

  // apply relocations created by relaxation
  BranchIslandFactory* br_factory = m_Backend.getBRIslandFactory();
  BranchIslandFactory::iterator facIter, facEnd = br_factory->end();
  for (facIter = br_factory->begin(); facIter != facEnd; ++facIter) {
    BranchIsland& island = *facIter;
    BranchIsland::reloc_iterator iter, iterEnd = island.reloc_end();
    for (iter = island.reloc_begin(); iter != iterEnd; ++iter)
      (*iter)->apply(*m_Backend.getRelocFactory());
  }
  return true;
}


void FragmentLinker::syncRelocationResult(MemoryArea& pOutput)
{
  if (LinkerConfig::Object != m_Config.codeGenType())
    normalSyncRelocationResult(pOutput);
  else
    partialSyncRelocationResult(pOutput);
  return;
}

void FragmentLinker::normalSyncRelocationResult(MemoryArea& pOutput)
{
  MemoryRegion* region = pOutput.request(0, pOutput.handler()->size());

  uint8_t* data = region->getBuffer();

  // sync relocations from inputs
  Module::reloc_data_iterator dataIter, dataEnd = m_Module.reloc_data_end();
  for (dataIter = m_Module.reloc_data_begin(); dataIter != dataEnd; ++dataIter) {
    RelocationData* reloc_data = *dataIter;
    RelocationData::iterator relocIter, relocEnd = reloc_data->end();
    for (relocIter = reloc_data->begin(); relocIter != relocEnd; ++relocIter) {
      Relocation* reloc = llvm::cast<Relocation>(relocIter);
      writeRelocationResult(*reloc, data);
    }
  }

  // sync relocations created by relaxation
  BranchIslandFactory* br_factory = m_Backend.getBRIslandFactory();
  BranchIslandFactory::iterator facIter, facEnd = br_factory->end();
  for (facIter = br_factory->begin(); facIter != facEnd; ++facIter) {
    BranchIsland& island = *facIter;
    BranchIsland::reloc_iterator iter, iterEnd = island.reloc_end();
    for (iter = island.reloc_begin(); iter != iterEnd; ++iter) {
      Relocation* reloc = *iter;
      writeRelocationResult(*reloc, data);
    }
  }

  pOutput.clear();
}

void FragmentLinker::partialSyncRelocationResult(MemoryArea& pOutput)
{
  MemoryRegion* region = pOutput.request(0, pOutput.handler()->size());

  uint8_t* data = region->getBuffer();

  // traverse outputs' LDSection to get RelocationData
  Module::iterator sectIter, sectEnd = m_Module.end();
  for (sectIter = m_Module.begin(); sectIter != sectEnd; ++sectIter) {
    if (LDFileFormat::Relocation != (*sectIter)->kind())
      continue;

    RelocationData* reloc_data = (*sectIter)->getRelocationData();
    RelocationData::iterator relocIter, relocEnd = reloc_data->end();
    for (relocIter = reloc_data->begin(); relocIter != relocEnd; ++relocIter) {
      Relocation* reloc = llvm::cast<Relocation>(relocIter);
      writeRelocationResult(*reloc, data);
    }
  }

  pOutput.clear();
}

void FragmentLinker::writeRelocationResult(Relocation& pReloc, uint8_t* pOutput)
{
  // get output file offset
  size_t out_offset =
            m_Layout.getOutputLDSection(*pReloc.targetRef().frag())->offset() +
            pReloc.targetRef().getOutputOffset();

  uint8_t* target_addr = pOutput + out_offset;
  // byte swapping if target and host has different endian, and then write back
  if(llvm::sys::isLittleEndianHost() != m_Backend.isLittleEndian()) {
     uint64_t tmp_data = 0;

     switch(m_Backend.bitclass()) {
       case 32u:
         tmp_data = bswap32(pReloc.target());
         std::memcpy(target_addr, &tmp_data, 4);
         break;

       case 64u:
         tmp_data = bswap64(pReloc.target());
         std::memcpy(target_addr, &tmp_data, 8);
         break;

       default:
         break;
    }
  }
  else
    std::memcpy(target_addr, &pReloc.target(), m_Backend.bitclass()/8);
}

/// isOutputPIC - return whether the output is position-independent
bool FragmentLinker::isOutputPIC() const
{
  static bool result = checkIsOutputPIC();
  return result;
}

/// isStaticLink - return whether we're doing static link
bool FragmentLinker::isStaticLink() const
{
  static bool result = checkIsStaticLink();
  return result;
}

bool FragmentLinker::checkIsOutputPIC() const
{
  if (LinkerConfig::DynObj == m_Config.codeGenType() ||
      m_Config.options().isPIE())
    return true;
  return false;
}

bool FragmentLinker::checkIsStaticLink() const
{
  if (m_Module.getLibraryList().empty())
    return true;
  return false;
}

