//===- ObjectBuilder.cpp --------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <mcld/Object/ObjectBuilder.h>

#include <mcld/Module.h>
#include <mcld/LinkerConfig.h>
#include <mcld/Object/SectionMap.h>
#include <mcld/LD/LDSection.h>
#include <mcld/LD/SectionData.h>
#include <mcld/LD/RelocData.h>
#include <mcld/LD/EhFrame.h>
#include <mcld/Fragment/Relocation.h>
#include <mcld/Fragment/AlignFragment.h>
#include <mcld/Fragment/NullFragment.h>

#include <llvm/Support/Casting.h>

using namespace mcld;

//===----------------------------------------------------------------------===//
// ObjectBuilder
//===----------------------------------------------------------------------===//
ObjectBuilder::ObjectBuilder(const LinkerConfig& pConfig, Module& pTheModule)
  : m_Config(pConfig), m_Module(pTheModule) {
}

/// CreateSection - create an output section.
LDSection* ObjectBuilder::CreateSection(const std::string& pName,
                                        LDFileFormat::Kind pKind,
                                        uint32_t pType,
                                        uint32_t pFlag,
                                        uint32_t pAlign)
{
  // try to get one from output LDSection
  const SectionMap::NamePair& pair = m_Config.scripts().sectionMap().find(pName);
  std::string output_name = (pair.isNull())?pName:pair.to;
  LDSection* output_sect = LDSection::Create(output_name, pKind, pType, pFlag);
  output_sect->setAlign(pAlign);
  m_Module.getSectionTable().push_back(output_sect);
  return output_sect;
}

/// CreateSectionData - To create a section data for given pSection.
SectionData* ObjectBuilder::CreateSectionData(LDSection& pSection)
{
  assert(!pSection.hasSectionData() && "pSection already has section data.");

  SectionData* sect_data = SectionData::Create(pSection);
  pSection.setSectionData(sect_data);
  return sect_data;
}

/// CreateRelocData - To create a relocation data for given pSection.
RelocData* ObjectBuilder::CreateRelocData(LDSection &pSection)
{
  assert(!pSection.hasRelocData() && "pSection already has relocation data.");

  RelocData* reloc_data = RelocData::Create(pSection);
  pSection.setRelocData(reloc_data);
  return reloc_data;
}

/// CreateEhFrame - To create a eh_frame for given pSection
EhFrame* ObjectBuilder::CreateEhFrame(LDSection& pSection)
{
  assert(!pSection.hasEhFrame() && "pSection already has eh_frame.");

  EhFrame* eh_frame = new EhFrame(pSection);
  pSection.setEhFrame(eh_frame);
  return eh_frame;
}

/// MergeSection - merge the pInput section to the pOutput section
bool ObjectBuilder::MergeSection(LDSection& pOutput, LDSection& pInput)
{
  return true;
}

/// AppendFragment - To append pFrag to the given LDSection pSection.
/// In order to keep the alignment of pFrag, This function may insert an
/// AlignFragment before pFrag to keep align constraint.
uint64_t ObjectBuilder::AppendFragment(Fragment& pFrag,
                                       LDSection& pSection,
                                       uint32_t pAlignConstraint)
{
  switch (pSection.kind()) {
    case LDFileFormat::Relocation: {
      Relocation* reloc = llvm::cast<Relocation>(&pFrag);
      assert(pSection.hasRelocData() &&
             "given LDSection does not have a relocation data.");
      RelocData* reloc_data = pSection.getRelocData();
      AppendRelocation(*reloc, *reloc_data);
      return 0;
    }
    case LDFileFormat::EhFrame: {
      assert(pSection.hasEhFrame() &&
             "given LDSection does not have a eh_frame.");

      EhFrame* eh_frame = pSection.getEhFrame();
      return AppendEhFrame(pFrag, *eh_frame, pAlignConstraint);
    }
    default: {
      assert(pSection.hasSectionData() &&
             "given LDSection does not have a section data.");

      SectionData* sect_data = pSection.getSectionData();
      return AppendFragment(pFrag, *sect_data, pAlignConstraint);
    }
  }
  return 0;
}

/// AppendFragment - To append pFrag to the given SectionData pSD.
uint64_t ObjectBuilder::AppendFragment(Fragment& pFrag,
                                       SectionData& pSD,
                                       uint32_t pAlignConstraint)
{
  // get initial offset.
  uint32_t offset = 0;
  if (!pSD.empty())
    offset = pSD.back().getOffset() + pSD.back().size();

  AlignFragment* align = NULL;
  if (pAlignConstraint > 1) {
    // if the align constraint is larger than 1, append an alignment
    align = new AlignFragment(pAlignConstraint, // alignment
                              0x0, // the filled value
                              1u,  // the size of filled value
                              pAlignConstraint - 1 // max bytes to emit
                              );
    align->setOffset(offset);
    align->setParent(&pSD);
    pSD.getFragmentList().push_back(align);
    offset += align->size();
  }

  // append the fragment
  pFrag.setParent(&pSD);
  pFrag.setOffset(offset);
  pSD.getFragmentList().push_back(&pFrag);

  // append the null fragment
  offset += pFrag.size();
  NullFragment* null = new NullFragment(&pSD);
  null->setOffset(offset);

  if (NULL != align)
    return align->size() + pFrag.size();
  else
    return pFrag.size();
}

/// AppendRelocation - To append an relocation to the given RelocData pRD.
void ObjectBuilder::AppendRelocation(Relocation& pRelocation, RelocData& pRD)
{
  pRD.getFragmentList().push_back(&pRelocation);
}

/// AppendEhFrame - To append a fragment to the given EhFrame pEhFram.
uint64_t ObjectBuilder::AppendEhFrame(Fragment& pFrag, EhFrame& pEhFrame,
                                      uint32_t pAlignConstraint)
{
  return AppendFragment(pFrag, pEhFrame.getSectionData(), pAlignConstraint);
}

/// AppendEhFrame - To append a FDE to the given EhFrame pEhFram.
uint64_t
ObjectBuilder::AppendEhFrame(EhFrame::FDE& pFDE, EhFrame& pEhFrame,
                             uint32_t pAlignConstraint)
{
  pEhFrame.addFDE(pFDE);
  return AppendEhFrame(pFDE, pEhFrame, pAlignConstraint);
}

/// AppendEhFrame - To append a CIE to the given EhFrame pEhFram.
uint64_t
ObjectBuilder::AppendEhFrame(EhFrame::CIE& pCIE, EhFrame& pEhFrame,
                             uint32_t pAlignConstraint)
{
  pEhFrame.addCIE(pCIE);
  return AppendEhFrame(pCIE, pEhFrame, pAlignConstraint);
}
