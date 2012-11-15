//===- Linker.cpp ---------------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <mcld/Linker.h>
#include <mcld/LinkerConfig.h>
#include <mcld/Module.h>

#include <mcld/Support/MsgHandling.h>
#include <mcld/Support/TargetRegistry.h>
#include <mcld/Support/FileHandle.h>
#include <mcld/Support/MemoryArea.h>
#include <mcld/Support/raw_ostream.h>

#include <mcld/LD/TextDiagnosticPrinter.h>
#include <mcld/Object/ObjectLinker.h>
#include <mcld/MC/InputBuilder.h>

#include <cassert>

using namespace mcld;

Linker::Linker()
  : m_pConfig(NULL), m_pModule(NULL),
    m_pTarget(NULL), m_pBackend(NULL), m_pPrinter(NULL),
    m_pInputBuilder(NULL), m_pObjLinker(NULL) {
}

Linker::Linker(const LinkerConfig& pConfig)
  : m_pConfig(&pConfig), m_pModule(NULL),
    m_pTarget(NULL), m_pBackend(NULL), m_pPrinter(NULL),
    m_pInputBuilder(NULL), m_pObjLinker(NULL) {
}

bool Linker::config(const LinkerConfig& pConfig)
{
  m_pConfig = &pConfig;

  if (!initDiagnosticEngine())
    return false;

  if (!initTarget())
    return false;

  if (!initBackend())
    return false;

  return true;
}

bool Linker::link(Module& pModule, InputTree& pInputTree)
{
  m_pInputBuilder = new InputBuilder(*m_pConfig);

  m_pObjLinker = new ObjectLinker(*m_pConfig,
                                  pModule,
                                  *m_pInputBuilder,
                                  *m_pBackend);

  // 2. - initialize FragmentLinker
  if (!m_pObjLinker->initFragmentLinker())
    return true;

  // 3. - initialize output's standard sections
  if (!m_pObjLinker->initStdSections())
    return true;

  // 4. - normalize the input tree
  m_pObjLinker->normalize();

  if (m_pConfig->options().trace()) {
    static int counter = 0;
    mcld::outs() << "** name\ttype\tpath\tsize (" << pModule.getInputTree().size() << ")\n";
    InputTree::const_dfs_iterator input, inEnd = pModule.getInputTree().dfs_end();
    for (input=pModule.getInputTree().dfs_begin(); input!=inEnd; ++input) {
      mcld::outs() << counter++ << " *  " << (*input)->name();
      switch((*input)->type()) {
      case Input::Archive:
        mcld::outs() << "\tarchive\t(";
        break;
      case Input::Object:
        mcld::outs() << "\tobject\t(";
        break;
      case Input::DynObj:
        mcld::outs() << "\tshared\t(";
        break;
      case Input::Script:
        mcld::outs() << "\tscript\t(";
        break;
      case Input::External:
        mcld::outs() << "\textern\t(";
        break;
      default:
        unreachable(diag::err_cannot_trace_file) << (*input)->type()
                                                 << (*input)->name()
                                                 << (*input)->path();
      }
      mcld::outs() << (*input)->path() << ")\n";
    }
  }

  // 5. - check if we can do static linking and if we use split-stack.
  if (!m_pObjLinker->linkable())
    return true;

  // 6. - read all relocation entries from input files
  m_pObjLinker->readRelocations();

  // 7. - merge all sections
  if (!m_pObjLinker->mergeSections())
    return true;

  // 8. - add standard symbols and target-dependent symbols
  // m_pObjLinker->addUndefSymbols();
  if (!m_pObjLinker->addStandardSymbols() ||
      !m_pObjLinker->addTargetSymbols())
    return true;

  // 9. - scan all relocation entries by output symbols.
  m_pObjLinker->scanRelocations();

  // 10.a - pre-layout
  m_pObjLinker->prelayout();

  // 10.b - linear layout
  m_pObjLinker->layout();

  // 10.c - post-layout (create segment, instruction relaxing)
  m_pObjLinker->postlayout();

  // 11. - finalize symbol value
  m_pObjLinker->finalizeSymbolValue();

  // 12. - apply relocations
  m_pObjLinker->relocation();

  return true;
}

bool Linker::emit(MemoryArea& pOutput)
{
  // 13. - write out output
  m_pObjLinker->emitOutput(pOutput);

  // 14. - post processing
  m_pObjLinker->postProcessing(pOutput);

  return true;
}

bool Linker::emit(const std::string& pPath)
{
  FileHandle file;
  FileHandle::Permission perm = 0755;
  if (!file.open(pPath,
            FileHandle::ReadWrite | FileHandle::Truncate | FileHandle::Create,
            perm)) {
    error(diag::err_cannot_open_output_file) << "Linker::emit()" << pPath;
    return false;
  }

  MemoryArea* output = new MemoryArea(file);

  bool result = emit(*output);

  delete output;
  file.close();
  return result;
}

bool Linker::emit(int pFileDescriptor)
{
  FileHandle file;
  file.delegate(pFileDescriptor);
  MemoryArea* output = new MemoryArea(file);

  bool result = emit(*output);

  delete output;
  file.close();
  return result;
}

bool Linker::reset()
{
  delete m_pInputBuilder;
  delete m_pObjLinker;
  return true;
}

bool Linker::initDiagnosticEngine()
{
  assert(NULL != m_pConfig);

  m_pPrinter = new TextDiagnosticPrinter(mcld::errs(), *m_pConfig);

  InitializeDiagnosticEngine(*m_pConfig, m_pPrinter);

  return true;
}

bool Linker::initTarget()
{
  assert(NULL != m_pConfig);

  std::string error;
  m_pTarget = TargetRegistry::lookupTarget(m_pConfig->triple().str(), error);
  if (NULL == m_pTarget) {
    fatal(diag::fatal_cannot_init_target) << m_pConfig->triple().str() << error;
    return false;
  }
  return true;
}

bool Linker::initBackend()
{
  assert(NULL != m_pTarget);
  m_pBackend = m_pTarget->createLDBackend(*m_pConfig);
  if (NULL == m_pBackend) {
    fatal(diag::fatal_cannot_init_backend) << m_pConfig->triple().str();
    return false;
  }
  return true;
}
