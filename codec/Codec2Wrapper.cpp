#include "Codec2Wrapper.h"
#include "net/packet.h"

#include <QtGlobal>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMutexLocker>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

#if defined(Q_OS_ANDROID)
#include <setjmp.h>
#include <signal.h>
#if defined(__aarch64__)
#include <ucontext.h>
#endif
#endif

namespace
{
QString normalizeDynamicLibraryPath(const QString& path, QString* error);
#if defined(Q_OS_ANDROID) && defined(INCOMUDON_USE_CODEC2)
template <typename Fn>
bool runGuardedCodec2InitCall(const char* callName, Fn&& fn);
#endif
}

#if defined(Q_OS_ANDROID)
#include <android/log.h>
#include <dlfcn.h>
#include <elf.h>
#include <QJniEnvironment>
#include <QJniObject>
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef INCOMUDON_USE_OPUS
void Codec2Wrapper::unloadOpusLibrary()
{
    if (!m_opusLibrary)
        return;
    if (m_opusLibrary->isLoaded())
        m_opusLibrary->unload();
}

void Codec2Wrapper::clearOpusApi()
{
    m_opusEncoderCreate = nullptr;
    m_opusEncoderDestroy = nullptr;
    m_opusEncode = nullptr;
    m_opusEncoderCtl = nullptr;
    m_opusDecoderCreate = nullptr;
    m_opusDecoderDestroy = nullptr;
    m_opusDecode = nullptr;
}

QString Codec2Wrapper::normalizeOpusLibraryPath(const QString& path, QString* error) const
{
    return normalizeDynamicLibraryPath(path, error);
}

void Codec2Wrapper::setOpusLibraryLoadedInternal(bool loaded)
{
    if (m_opusLibraryLoaded == loaded)
        return;
    m_opusLibraryLoaded = loaded;
    emit opusLibraryLoadedChanged();
}

void Codec2Wrapper::setOpusLibraryErrorInternal(const QString& error)
{
    if (m_opusLibraryError == error)
        return;
    m_opusLibraryError = error;
    emit opusLibraryErrorChanged();
}

void Codec2Wrapper::refreshOpusLibrary()
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (!m_opusLibrary)
        return;

    clearOpusApi();
    unloadOpusLibrary();

    const bool explicitPath = !m_opusLibraryPath.trimmed().isEmpty();
    QString normalizedError;
    const QString normalizedPath = normalizeOpusLibraryPath(m_opusLibraryPath, &normalizedError);
    if (explicitPath && normalizedPath.isEmpty())
    {
        setOpusLibraryLoadedInternal(false);
        if (normalizedError.isEmpty())
            normalizedError = QStringLiteral("Invalid opus library path");
        setOpusLibraryErrorInternal(normalizedError);
        return;
    }

    // Runtime loading is optional and used only when user specifies a path.
    if (!explicitPath)
    {
        setOpusLibraryLoadedInternal(false);
        setOpusLibraryErrorInternal(QString());
        return;
    }

    m_opusLibrary->setFileName(normalizedPath);
    if (!m_opusLibrary->load())
    {
        setOpusLibraryLoadedInternal(false);
        setOpusLibraryErrorInternal(QStringLiteral("Failed to load opus library: %1")
                                        .arg(m_opusLibrary->errorString()));
        return;
    }

    m_opusEncoderCreate = reinterpret_cast<OpusEncoderCreateFn>(
        m_opusLibrary->resolve("opus_encoder_create"));
    m_opusEncoderDestroy = reinterpret_cast<OpusEncoderDestroyFn>(
        m_opusLibrary->resolve("opus_encoder_destroy"));
    m_opusEncode = reinterpret_cast<OpusEncodeFn>(
        m_opusLibrary->resolve("opus_encode"));
    m_opusEncoderCtl = reinterpret_cast<OpusEncoderCtlFn>(
        m_opusLibrary->resolve("opus_encoder_ctl"));
    m_opusDecoderCreate = reinterpret_cast<OpusDecoderCreateFn>(
        m_opusLibrary->resolve("opus_decoder_create"));
    m_opusDecoderDestroy = reinterpret_cast<OpusDecoderDestroyFn>(
        m_opusLibrary->resolve("opus_decoder_destroy"));
    m_opusDecode = reinterpret_cast<OpusDecodeFn>(
        m_opusLibrary->resolve("opus_decode"));

    const bool symbolsOk = m_opusEncoderCreate &&
                           m_opusEncoderDestroy &&
                           m_opusEncode &&
                           m_opusEncoderCtl &&
                           m_opusDecoderCreate &&
                           m_opusDecoderDestroy &&
                           m_opusDecode;
    if (symbolsOk)
    {
        setOpusLibraryLoadedInternal(true);
        setOpusLibraryErrorInternal(QString());
        return;
    }

    clearOpusApi();
    unloadOpusLibrary();
    setOpusLibraryLoadedInternal(false);
    setOpusLibraryErrorInternal(
        QStringLiteral("Required opus symbols were not found in: %1").arg(normalizedPath));
}
#endif

#ifdef INCOMUDON_USE_OPUS
#include <opus/opus.h>
#endif

namespace
{
#ifdef INCOMUDON_USE_CODEC2
// Keep local mode constants so build does not depend on codec2 public headers.
constexpr int kCodec2Mode3200 = 0;
constexpr int kCodec2Mode2400 = 1;
constexpr int kCodec2Mode1600 = 2;
constexpr int kCodec2Mode700C = 8;
constexpr int kCodec2Mode450 = 10;
constexpr int kIncomUdonCodec2AbiVersion = 2026022801;
#endif

#ifdef INCOMUDON_USE_CODEC2
QRecursiveMutex& codec2ApiMutex()
{
    static QRecursiveMutex mutex;
    return mutex;
}
#endif

void logCodec2Status(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
#if defined(Q_OS_ANDROID)
    __android_log_vprint(ANDROID_LOG_WARN, "IncomUdon", fmt, args);
#else
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    qWarning("%s", buffer);
#endif
    va_end(args);
}

template <typename Fn>
bool runGuardedCodec2Call(const char* callName, Fn&& fn)
{
#if defined(Q_OS_ANDROID) && defined(INCOMUDON_USE_CODEC2)
    return runGuardedCodec2InitCall(callName, std::forward<Fn>(fn));
#else
    Q_UNUSED(callName);
    fn();
    return true;
#endif
}

#if defined(Q_OS_ANDROID) && defined(INCOMUDON_USE_CODEC2)
thread_local sigjmp_buf* g_codec2GuardJmpBuf = nullptr;
thread_local const char* g_codec2GuardCallName = nullptr;
thread_local void* g_codec2GuardFaultAddr = nullptr;
thread_local int g_codec2GuardSiCode = 0;
#if defined(__aarch64__)
thread_local void* g_codec2GuardPc = nullptr;
#endif

struct Codec2GuardSigActions
{
    struct sigaction segv {};
    bool hasSegv = false;
#ifdef SIGBUS
    struct sigaction bus {};
    bool hasBus = false;
#endif
#ifdef SIGABRT
    struct sigaction abrt {};
    bool hasAbrt = false;
#endif
};

Codec2GuardSigActions& codec2GuardOldSigActions()
{
    static Codec2GuardSigActions actions;
    return actions;
}

std::mutex& codec2GuardInstallMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::atomic_bool& codec2GuardInstalled()
{
    static std::atomic_bool installed{false};
    return installed;
}

void codec2GuardSignalForward(int sig, siginfo_t* info, void* ucontext)
{
    const Codec2GuardSigActions& old = codec2GuardOldSigActions();
    const struct sigaction* prev = nullptr;
    if (sig == SIGSEGV && old.hasSegv)
        prev = &old.segv;
#ifdef SIGBUS
    else if (sig == SIGBUS && old.hasBus)
        prev = &old.bus;
#endif
#ifdef SIGABRT
    else if (sig == SIGABRT && old.hasAbrt)
        prev = &old.abrt;
#endif

    if (prev)
    {
        if ((prev->sa_flags & SA_SIGINFO) && prev->sa_sigaction)
        {
            prev->sa_sigaction(sig, info, ucontext);
            return;
        }
        if (prev->sa_handler == SIG_IGN)
            return;
        if (prev->sa_handler && prev->sa_handler != SIG_DFL)
        {
            prev->sa_handler(sig);
            return;
        }
    }

    signal(sig, SIG_DFL);
    raise(sig);
}

void codec2GuardSignalHandler(int sig, siginfo_t* info, void* ucontext)
{
    if (g_codec2GuardJmpBuf)
    {
        g_codec2GuardFaultAddr = info ? info->si_addr : nullptr;
        g_codec2GuardSiCode = info ? info->si_code : 0;
#if defined(__aarch64__)
        g_codec2GuardPc = nullptr;
        if (ucontext)
        {
            auto* uc = reinterpret_cast<ucontext_t*>(ucontext);
            g_codec2GuardPc = reinterpret_cast<void*>(uc->uc_mcontext.pc);
        }
#endif
        siglongjmp(*g_codec2GuardJmpBuf, sig);
    }
    codec2GuardSignalForward(sig, info, ucontext);
}

void ensureCodec2GuardHandlerInstalled()
{
    if (codec2GuardInstalled().load(std::memory_order_acquire))
        return;

    std::lock_guard<std::mutex> lock(codec2GuardInstallMutex());
    if (codec2GuardInstalled().load(std::memory_order_relaxed))
        return;

    struct sigaction sa {};
    sa.sa_sigaction = codec2GuardSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;

    Codec2GuardSigActions& old = codec2GuardOldSigActions();
    if (sigaction(SIGSEGV, &sa, &old.segv) == 0)
        old.hasSegv = true;
#ifdef SIGBUS
    if (sigaction(SIGBUS, &sa, &old.bus) == 0)
        old.hasBus = true;
#endif
#ifdef SIGABRT
    if (sigaction(SIGABRT, &sa, &old.abrt) == 0)
        old.hasAbrt = true;
#endif

    codec2GuardInstalled().store(true, std::memory_order_release);
}

template <typename Fn>
bool runGuardedCodec2InitCall(const char* callName, Fn&& fn)
{
    ensureCodec2GuardHandlerInstalled();

    sigjmp_buf env;
    g_codec2GuardCallName = callName;
    g_codec2GuardJmpBuf = &env;
    g_codec2GuardFaultAddr = nullptr;
    g_codec2GuardSiCode = 0;
#if defined(__aarch64__)
    g_codec2GuardPc = nullptr;
#endif
    int sig = 0;
    const int jumpCode = sigsetjmp(env, 1);
    if (jumpCode == 0)
    {
        fn();
    }
    else
    {
        sig = jumpCode;
    }
    g_codec2GuardJmpBuf = nullptr;
    g_codec2GuardCallName = nullptr;

    if (sig != 0)
    {
#if defined(__aarch64__)
        logCodec2Status("codec2 runtime call crashed: %s (signal=%d si_code=%d fault=%p pc=%p)",
                        callName ? callName : "unknown",
                        sig,
                        g_codec2GuardSiCode,
                        g_codec2GuardFaultAddr,
                        g_codec2GuardPc);
#else
        logCodec2Status("codec2 runtime call crashed: %s (signal=%d)",
                        callName ? callName : "unknown",
                        sig);
#endif
        return false;
    }
    return true;
}
#else
template <typename Fn>
bool runGuardedCodec2InitCall(const char*, Fn&& fn)
{
    fn();
    return true;
}
#endif

#if defined(Q_OS_ANDROID)
QString copyAndroidContentUriToLocalPath(const QString& uriText, QString* error)
{
    if (error)
        error->clear();

    QJniObject jUriText = QJniObject::fromString(uriText);
    QJniObject jResult = QJniObject::callStaticObjectMethod(
        "com/friedoudon/incomudon/IncomUdonActivity",
        "copyContentUriToLocalLib",
        "(Ljava/lang/String;)Ljava/lang/String;",
        jUriText.object<jstring>());

    QJniEnvironment env;
    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        if (error)
            *error = QStringLiteral("Failed to access Android content URI");
        return QString();
    }

    if (!jResult.isValid())
    {
        if (error)
            *error = QStringLiteral("Failed to resolve content URI: %1").arg(uriText);
        return QString();
    }

    const QString localPath = jResult.toString();
    if (localPath.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Could not copy selected library to app storage");
        return QString();
    }
    return QDir::toNativeSeparators(localPath);
}
#endif

#if defined(Q_OS_ANDROID) && defined(INCOMUDON_USE_CODEC2)
bool patchAndroidJumpSlotsFromFile(const QString& soPath,
                                   void* loadBase,
                                   QString* errorOut,
                                   int* patchedOut)
{
    if (errorOut)
        errorOut->clear();
    if (patchedOut)
        *patchedOut = 0;
    if (!loadBase)
    {
        if (errorOut)
            *errorOut = QStringLiteral("load base is null");
        return false;
    }

    QFile file(soPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorOut)
            *errorOut = QStringLiteral("failed to open %1").arg(soPath);
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    if (bytes.size() < static_cast<qsizetype>(sizeof(Elf64_Ehdr)))
    {
        if (errorOut)
            *errorOut = QStringLiteral("ELF header is too small");
        return false;
    }

    const auto* eh = reinterpret_cast<const Elf64_Ehdr*>(bytes.constData());
    if (std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 ||
        eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_machine != EM_AARCH64)
    {
        if (errorOut)
            *errorOut = QStringLiteral("unsupported ELF format (need ELF64 AArch64)");
        return false;
    }

    const quint64 phdrBytes = static_cast<quint64>(eh->e_phnum) * sizeof(Elf64_Phdr);
    if (static_cast<quint64>(eh->e_phoff) + phdrBytes > static_cast<quint64>(bytes.size()))
    {
        if (errorOut)
            *errorOut = QStringLiteral("program headers are out of range");
        return false;
    }

    const auto* phdrs = reinterpret_cast<const Elf64_Phdr*>(bytes.constData() + eh->e_phoff);
    const auto vaddrToOffset = [&](quint64 vaddr, quint64* fileOffset) -> bool {
        for (int i = 0; i < eh->e_phnum; ++i)
        {
            const Elf64_Phdr& ph = phdrs[i];
            if (ph.p_type != PT_LOAD)
                continue;

            const quint64 start = ph.p_vaddr;
            const quint64 end = ph.p_vaddr + ph.p_memsz;
            if (vaddr < start || vaddr >= end)
                continue;

            const quint64 delta = vaddr - start;
            if (delta > ph.p_filesz)
                return false;
            *fileOffset = ph.p_offset + delta;
            return *fileOffset <= static_cast<quint64>(bytes.size());
        }
        return false;
    };

    const Elf64_Phdr* dynamicPh = nullptr;
    for (int i = 0; i < eh->e_phnum; ++i)
    {
        if (phdrs[i].p_type == PT_DYNAMIC)
        {
            dynamicPh = &phdrs[i];
            break;
        }
    }
    if (!dynamicPh)
    {
        if (errorOut)
            *errorOut = QStringLiteral("PT_DYNAMIC was not found");
        return false;
    }
    if (dynamicPh->p_offset + dynamicPh->p_filesz > static_cast<quint64>(bytes.size()))
    {
        if (errorOut)
            *errorOut = QStringLiteral("PT_DYNAMIC is out of range");
        return false;
    }

    quint64 strtabVaddr = 0;
    quint64 symtabVaddr = 0;
    quint64 strtabSize = 0;
    quint64 jmprelVaddr = 0;
    quint64 pltrelSize = 0;
    quint64 relaVaddr = 0;
    quint64 relaSize = 0;
    quint64 relaEntSize = sizeof(Elf64_Rela);
    quint64 symEntSize = sizeof(Elf64_Sym);
    quint64 pltRelType = 0;

    const auto* dynEntries = reinterpret_cast<const Elf64_Dyn*>(bytes.constData() + dynamicPh->p_offset);
    const int dynCount = static_cast<int>(dynamicPh->p_filesz / sizeof(Elf64_Dyn));
    for (int i = 0; i < dynCount; ++i)
    {
        const Elf64_Dyn& d = dynEntries[i];
        switch (d.d_tag)
        {
        case DT_NULL:
            i = dynCount;
            break;
        case DT_STRTAB:
            strtabVaddr = d.d_un.d_ptr;
            break;
        case DT_STRSZ:
            strtabSize = d.d_un.d_val;
            break;
        case DT_SYMTAB:
            symtabVaddr = d.d_un.d_ptr;
            break;
        case DT_SYMENT:
            symEntSize = d.d_un.d_val;
            break;
        case DT_JMPREL:
            jmprelVaddr = d.d_un.d_ptr;
            break;
        case DT_PLTRELSZ:
            pltrelSize = d.d_un.d_val;
            break;
        case DT_PLTREL:
            pltRelType = d.d_un.d_val;
            break;
        case DT_RELA:
            relaVaddr = d.d_un.d_ptr;
            break;
        case DT_RELASZ:
            relaSize = d.d_un.d_val;
            break;
        case DT_RELAENT:
            relaEntSize = d.d_un.d_val;
            break;
        default:
            break;
        }
    }

    if (!strtabVaddr || !symtabVaddr || !jmprelVaddr || pltrelSize == 0 ||
        !relaVaddr || relaSize == 0 || pltRelType != DT_RELA ||
        symEntSize < sizeof(Elf64_Sym) || relaEntSize != sizeof(Elf64_Rela))
    {
        if (errorOut)
            *errorOut = QStringLiteral("required dynamic tags are missing or unsupported");
        return false;
    }

    quint64 strtabOff = 0;
    quint64 symtabOff = 0;
    quint64 jmprelOff = 0;
    quint64 relaOff = 0;
    if (!vaddrToOffset(strtabVaddr, &strtabOff) ||
        !vaddrToOffset(symtabVaddr, &symtabOff) ||
        !vaddrToOffset(jmprelVaddr, &jmprelOff) ||
        !vaddrToOffset(relaVaddr, &relaOff))
    {
        if (errorOut)
            *errorOut = QStringLiteral("failed to map dynamic vaddr to file offsets");
        return false;
    }

    if (strtabOff + strtabSize > static_cast<quint64>(bytes.size()) ||
        jmprelOff + pltrelSize > static_cast<quint64>(bytes.size()) ||
        relaOff + relaSize > static_cast<quint64>(bytes.size()))
    {
        if (errorOut)
            *errorOut = QStringLiteral("dynamic sections are out of file bounds");
        return false;
    }

    const char* strtab = bytes.constData() + strtabOff;
    const auto* pltRela = reinterpret_cast<const Elf64_Rela*>(bytes.constData() + jmprelOff);
    const qsizetype pltRelaCount = static_cast<qsizetype>(pltrelSize / sizeof(Elf64_Rela));
    const auto* dynRela = reinterpret_cast<const Elf64_Rela*>(bytes.constData() + relaOff);
    const qsizetype dynRelaCount = static_cast<qsizetype>(relaSize / sizeof(Elf64_Rela));

    long pageSizeLong = sysconf(_SC_PAGESIZE);
    if (pageSizeLong <= 0)
        pageSizeLong = 4096;
    const quintptr pageSize = static_cast<quintptr>(pageSizeLong);

    QVector<quintptr> writablePages;
    writablePages.reserve(8);
    int patched = 0;

    const auto symbolAddress = [&](const Elf64_Sym* sym, const char* name) -> quintptr {
        if (!sym)
            return 0;
        if (sym->st_shndx != SHN_UNDEF)
            return reinterpret_cast<quintptr>(loadBase) + static_cast<quintptr>(sym->st_value);
        if (!name || name[0] == '\0')
            return 0;
        void* resolved = dlsym(RTLD_DEFAULT, name);
        return reinterpret_cast<quintptr>(resolved);
    };

    const auto patchReloc = [&](const Elf64_Rela& r, int relocType) {
        if (ELF64_R_TYPE(r.r_info) != static_cast<quint64>(relocType))
            return;

        const quint64 symIndex = ELF64_R_SYM(r.r_info);
        const quint64 symOff = symtabOff + symIndex * symEntSize;
        if (symOff + sizeof(Elf64_Sym) > static_cast<quint64>(bytes.size()))
            return;

        const auto* sym = reinterpret_cast<const Elf64_Sym*>(bytes.constData() + symOff);
        const char* name = nullptr;
        if (sym->st_name < strtabSize)
            name = strtab + sym->st_name;

        quintptr target = symbolAddress(sym, name);
        if (!target)
            return;
        target += static_cast<quintptr>(r.r_addend);

        auto* slot = reinterpret_cast<quintptr*>(reinterpret_cast<char*>(loadBase) + r.r_offset);
        if (*slot == target)
            return;

        const quintptr page = reinterpret_cast<quintptr>(slot) & ~(pageSize - 1u);
        if (!writablePages.contains(page))
        {
            if (mprotect(reinterpret_cast<void*>(page), pageSize, PROT_READ | PROT_WRITE) != 0)
                return;
            writablePages.push_back(page);
        }

        *slot = target;
        ++patched;
    };

    const auto patchRelativeReloc = [&](const Elf64_Rela& r) {
        if (ELF64_R_TYPE(r.r_info) != R_AARCH64_RELATIVE)
            return;

        auto* slot = reinterpret_cast<quintptr*>(reinterpret_cast<char*>(loadBase) + r.r_offset);
        const quintptr target = reinterpret_cast<quintptr>(loadBase) + static_cast<quintptr>(r.r_addend);
        if (*slot == target)
            return;

        const quintptr page = reinterpret_cast<quintptr>(slot) & ~(pageSize - 1u);
        if (!writablePages.contains(page))
        {
            if (mprotect(reinterpret_cast<void*>(page), pageSize, PROT_READ | PROT_WRITE) != 0)
                return;
            writablePages.push_back(page);
        }

        *slot = target;
        ++patched;
    };

    for (qsizetype i = 0; i < pltRelaCount; ++i)
    {
        patchReloc(pltRela[i], R_AARCH64_JUMP_SLOT);
    }
    for (qsizetype i = 0; i < dynRelaCount; ++i)
    {
        patchRelativeReloc(dynRela[i]);
        patchReloc(dynRela[i], R_AARCH64_GLOB_DAT);
    }

    if (patchedOut)
        *patchedOut = patched;
    return true;
}
#endif

QString normalizeDynamicLibraryPath(const QString& path, QString* error)
{
    if (error)
        error->clear();

    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return QString();

    const QUrl url(trimmed);
    if (url.isValid())
    {
        const QString scheme = url.scheme().toLower();
        if (scheme == QStringLiteral("file"))
            return QDir::toNativeSeparators(url.toLocalFile());
#if defined(Q_OS_ANDROID)
        if (scheme == QStringLiteral("content"))
            return copyAndroidContentUriToLocalPath(trimmed, error);
#endif
    }
#if defined(Q_OS_ANDROID)
    if (trimmed.startsWith(QStringLiteral("content://"), Qt::CaseInsensitive))
        return copyAndroidContentUriToLocalPath(trimmed, error);
#endif
    return QDir::toNativeSeparators(trimmed);
}
}

Codec2Wrapper::Codec2Wrapper(QObject* parent)
    : QObject(parent)
{
#ifdef INCOMUDON_USE_CODEC2
    m_codec2Library = new QLibrary(this);
    // Runtime-loaded codec2 builds can crash on first PLT call on some Android
    // linkers when loaded lazily. Force eager resolution (RTLD_NOW-like).
    m_codec2Library->setLoadHints(QLibrary::ResolveAllSymbolsHint);
    refreshCodec2Library();
    logCodec2Status("INCOMUDON_USE_CODEC2=1 (runtime load)");
#else
    m_codec2LibraryError = QStringLiteral("Codec2 support disabled at build time");
    logCodec2Status("INCOMUDON_USE_CODEC2=0 (disabled at build time)");
#endif
#ifdef INCOMUDON_USE_OPUS
    m_opusLibrary = new QLibrary(this);
    m_opusLibrary->setLoadHints(QLibrary::ResolveAllSymbolsHint);
    refreshOpusLibrary();
#else
    m_opusLibraryError = QStringLiteral("Opus support disabled at build time");
#endif
    updateCodec();
}

Codec2Wrapper::~Codec2Wrapper()
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
#ifdef INCOMUDON_USE_CODEC2
    if (m_codec && m_codec2Destroy)
    {
        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
        m_codec2Destroy(m_codec);
        m_codec = nullptr;
    }
    unloadCodec2Library();
    delete m_codec2Library;
    m_codec2Library = nullptr;
#endif
#ifdef INCOMUDON_USE_OPUS
    if (m_opusEncoder)
    {
        if (m_opusUsingRuntimeApi && m_opusEncoderDestroy)
            m_opusEncoderDestroy(m_opusEncoder);
        else
            opus_encoder_destroy(m_opusEncoder);
        m_opusEncoder = nullptr;
    }
    if (m_opusDecoder)
    {
        if (m_opusUsingRuntimeApi && m_opusDecoderDestroy)
            m_opusDecoderDestroy(m_opusDecoder);
        else
            opus_decoder_destroy(m_opusDecoder);
        m_opusDecoder = nullptr;
    }
    m_opusUsingRuntimeApi = false;
    unloadOpusLibrary();
    delete m_opusLibrary;
    m_opusLibrary = nullptr;
#endif
}

int Codec2Wrapper::codecType() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codecType;
}

void Codec2Wrapper::setCodecType(int type)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    int normalized = CodecTypeCodec2;
    if (type == CodecTypeOpus)
        normalized = CodecTypeOpus;

#ifndef INCOMUDON_USE_OPUS
    if (normalized == CodecTypeOpus)
        normalized = CodecTypeCodec2;
#endif

    if (m_codecType == normalized)
        return;

    m_codecType = normalized;
    updateCodec();
    emit codecTypeChanged();
}

int Codec2Wrapper::mode() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_mode;
}

void Codec2Wrapper::setMode(int mode)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    const int normalized = normalizeMode(mode);
    if (m_mode == normalized)
        return;

    m_mode = normalized;
    updateCodec();
    emit modeChanged();
}

int Codec2Wrapper::frameBytes() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_frameBytes;
}

void Codec2Wrapper::setFrameBytes(int bytes)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_frameBytes == bytes)
        return;

    m_frameBytes = bytes;
    emit frameBytesChanged();
}

int Codec2Wrapper::pcmFrameBytes() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_pcmFrameBytes;
}

int Codec2Wrapper::frameMs() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_frameMs;
}

bool Codec2Wrapper::forcePcm() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_forcePcm;
}

void Codec2Wrapper::setForcePcm(bool force)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_forcePcm == force)
        return;

    m_forcePcm = force;
    updateCodec();
    emit forcePcmChanged();
}

bool Codec2Wrapper::codec2Active() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codec2Active;
}

bool Codec2Wrapper::opusActive() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_opusActive;
}

QString Codec2Wrapper::opusLibraryPath() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_opusLibraryPath;
}

void Codec2Wrapper::setOpusLibraryPath(const QString& path)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_opusLibraryPath == path)
        return;

    m_opusLibraryPath = path;
    emit opusLibraryPathChanged();

#ifdef INCOMUDON_USE_OPUS
    refreshOpusLibrary();
#else
    if (m_opusLibraryError != QStringLiteral("Opus support disabled at build time"))
    {
        m_opusLibraryError = QStringLiteral("Opus support disabled at build time");
        emit opusLibraryErrorChanged();
    }
#endif
    updateCodec();
}

bool Codec2Wrapper::opusLibraryLoaded() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_opusLibraryLoaded;
}

QString Codec2Wrapper::opusLibraryError() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_opusLibraryError;
}

int Codec2Wrapper::activeCodecTransportId() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_forcePcm)
        return Proto::CODEC_TRANSPORT_PCM;
    if (m_opusActive)
        return Proto::CODEC_TRANSPORT_OPUS;
    if (m_codec2Active)
        return Proto::CODEC_TRANSPORT_CODEC2;
    return Proto::CODEC_TRANSPORT_PCM;
}

QString Codec2Wrapper::codec2LibraryPath() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codec2LibraryPath;
}

void Codec2Wrapper::setCodec2LibraryPath(const QString& path)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_codec2LibraryPath == path)
        return;

    m_codec2LibraryPath = path;
    m_codec2KnownBadPath.clear();
    emit codec2LibraryPathChanged();

#ifdef INCOMUDON_USE_CODEC2
    refreshCodec2Library();
#else
    if (m_codec2LibraryError != QStringLiteral("Codec2 support disabled at build time"))
    {
        m_codec2LibraryError = QStringLiteral("Codec2 support disabled at build time");
        emit codec2LibraryErrorChanged();
    }
#endif

    updateCodec();
}

bool Codec2Wrapper::codec2LibraryLoaded() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codec2LibraryLoaded;
}

QString Codec2Wrapper::codec2LibraryError() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codec2LibraryError;
}

QByteArray Codec2Wrapper::encode(const QByteArray& pcmFrame) const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_forcePcm || m_pcmFrameBytes <= 0)
        return pcmFrame;

#ifdef INCOMUDON_USE_OPUS
    if (m_opusActive && m_codecType == CodecTypeOpus && m_opusEncoder)
    {
        const int expectedSamples = m_pcmFrameBytes / static_cast<int>(sizeof(opus_int16));
        QVector<opus_int16> inputSamples(expectedSamples, 0);
        const int copyBytes = qMin(pcmFrame.size(), m_pcmFrameBytes);
        if (copyBytes > 0)
            std::memcpy(inputSamples.data(), pcmFrame.constData(), copyBytes);

        QByteArray output(512, 0);
        const int encodedBytes = (m_opusUsingRuntimeApi && m_opusEncode)
            ? m_opusEncode(m_opusEncoder,
                           inputSamples.constData(),
                           expectedSamples,
                           reinterpret_cast<unsigned char*>(output.data()),
                           output.size())
            : opus_encode(m_opusEncoder,
                          inputSamples.constData(),
                          expectedSamples,
                          reinterpret_cast<unsigned char*>(output.data()),
                          output.size());
        if (encodedBytes <= 0)
            return QByteArray();
        output.truncate(encodedBytes);
        return output;
    }
#endif

#ifdef INCOMUDON_USE_CODEC2
    if (!m_codec || m_frameBytes <= 0)
        return pcmFrame;

    Codec2EncodeFn encodeFn = m_codec2EncodeActive ? m_codec2EncodeActive : m_codec2Encode;
    if (!encodeFn)
        return pcmFrame;

    const int samples = m_pcmFrameBytes / static_cast<int>(sizeof(short));
    QVector<short> inputSamples(samples);

    const int copyBytes = qMin(pcmFrame.size(), m_pcmFrameBytes);
    if (copyBytes > 0)
        std::memcpy(inputSamples.data(), pcmFrame.constData(), copyBytes);
    if (copyBytes < m_pcmFrameBytes)
        std::memset(reinterpret_cast<char*>(inputSamples.data()) + copyBytes, 0,
                    m_pcmFrameBytes - copyBytes);

    QByteArray output(m_frameBytes, 0);
    bool encodeOk = true;
    {
        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
        encodeOk = runGuardedCodec2Call("codec2_encode", [&]() {
            encodeFn(m_codec,
                     reinterpret_cast<unsigned char*>(output.data()),
                     inputSamples.data());
        });
    }
    if (!encodeOk)
        return QByteArray();
    return output;
#else
    return pcmFrame;
#endif
}

QByteArray Codec2Wrapper::decode(const QByteArray& codecFrame) const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_forcePcm || m_pcmFrameBytes <= 0)
        return codecFrame;

#ifdef INCOMUDON_USE_OPUS
    if (m_opusActive && m_codecType == CodecTypeOpus && m_opusDecoder)
    {
        const int expectedSamples = m_pcmFrameBytes / static_cast<int>(sizeof(opus_int16));
        QVector<opus_int16> outputSamples(expectedSamples, 0);
        const int decodedSamples = (m_opusUsingRuntimeApi && m_opusDecode)
            ? m_opusDecode(
                m_opusDecoder,
                codecFrame.isEmpty()
                    ? nullptr
                    : reinterpret_cast<const unsigned char*>(codecFrame.constData()),
                codecFrame.size(),
                outputSamples.data(),
                expectedSamples,
                0)
            : opus_decode(
                m_opusDecoder,
                codecFrame.isEmpty()
                    ? nullptr
                    : reinterpret_cast<const unsigned char*>(codecFrame.constData()),
                codecFrame.size(),
                outputSamples.data(),
                expectedSamples,
                0);

        QByteArray output(m_pcmFrameBytes, 0);
        if (decodedSamples <= 0)
            return output;

        const int copySamples = qMin(decodedSamples, expectedSamples);
        const int copyBytes = copySamples * static_cast<int>(sizeof(opus_int16));
        if (copyBytes > 0)
            std::memcpy(output.data(), outputSamples.constData(), copyBytes);
        return output;
    }
#endif

#ifdef INCOMUDON_USE_CODEC2
    if (!m_codec || m_frameBytes <= 0)
        return QByteArray(m_pcmFrameBytes, 0);

    Codec2DecodeFn decodeFn = m_codec2DecodeActive ? m_codec2DecodeActive : m_codec2Decode;
    if (!decodeFn)
        return QByteArray(m_pcmFrameBytes, 0);

    QByteArray input = codecFrame;
    if (input.size() < m_frameBytes)
        input.append(QByteArray(m_frameBytes - input.size(), 0));
    else if (input.size() > m_frameBytes)
        input.truncate(m_frameBytes);

    const int samples = m_pcmFrameBytes / static_cast<int>(sizeof(short));
    QVector<short> outputSamples(samples);
    bool decodeOk = true;
    {
        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
        decodeOk = runGuardedCodec2Call("codec2_decode", [&]() {
            decodeFn(m_codec,
                     outputSamples.data(),
                     reinterpret_cast<const unsigned char*>(input.constData()));
        });
    }
    if (!decodeOk)
        return QByteArray(m_pcmFrameBytes, 0);

    QByteArray output(m_pcmFrameBytes, 0);
    std::memcpy(output.data(), outputSamples.data(), m_pcmFrameBytes);
    return output;
#else
    return codecFrame;
#endif
}

void Codec2Wrapper::updateCodec()
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);

#ifdef INCOMUDON_USE_CODEC2
    if (m_codec && m_codec2Destroy)
    {
        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
        m_codec2Destroy(m_codec);
        m_codec = nullptr;
    }
#endif
#ifdef INCOMUDON_USE_OPUS
    if (m_opusEncoder)
    {
        if (m_opusUsingRuntimeApi && m_opusEncoderDestroy)
            m_opusEncoderDestroy(m_opusEncoder);
        else
            opus_encoder_destroy(m_opusEncoder);
        m_opusEncoder = nullptr;
    }
    if (m_opusDecoder)
    {
        if (m_opusUsingRuntimeApi && m_opusDecoderDestroy)
            m_opusDecoderDestroy(m_opusDecoder);
        else
            opus_decoder_destroy(m_opusDecoder);
        m_opusDecoder = nullptr;
    }
    m_opusUsingRuntimeApi = false;
#endif

    const bool shouldUseCodec = !m_forcePcm;
    const bool requestOpus = shouldUseCodec && (m_codecType == CodecTypeOpus);
    const bool requestCodec2 = shouldUseCodec && !requestOpus;

#ifdef INCOMUDON_USE_OPUS
    if (requestOpus)
    {
        if (!m_opusLibraryPath.trimmed().isEmpty() && !m_opusLibraryLoaded)
            refreshOpusLibrary();

        const bool useRuntimeApi = m_opusLibraryLoaded &&
                                   m_opusEncoderCreate &&
                                   m_opusDecoderCreate &&
                                   m_opusEncoderDestroy &&
                                   m_opusDecoderDestroy &&
                                   m_opusEncode &&
                                   m_opusDecode &&
                                   m_opusEncoderCtl;
        int encErr = 0;
        int decErr = 0;
        if (useRuntimeApi)
        {
            m_opusEncoder = m_opusEncoderCreate(8000, 1, OPUS_APPLICATION_VOIP, &encErr);
            m_opusDecoder = m_opusDecoderCreate(8000, 1, &decErr);
            m_opusUsingRuntimeApi = true;
        }
        else
        {
            m_opusEncoder = opus_encoder_create(8000, 1, OPUS_APPLICATION_VOIP, &encErr);
            m_opusDecoder = opus_decoder_create(8000, 1, &decErr);
            m_opusUsingRuntimeApi = false;
        }
        if (m_opusEncoder && m_opusDecoder &&
            encErr == OPUS_OK && decErr == OPUS_OK)
        {
            const int bitrate = opusBitrateForMode(m_mode);
            if (m_opusUsingRuntimeApi && m_opusEncoderCtl)
            {
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_BITRATE(bitrate));
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_VBR(0));
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_DTX(0));
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_COMPLEXITY(5));
            }
            else
            {
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_BITRATE(bitrate));
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_VBR(0));
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_DTX(0));
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_COMPLEXITY(5));
            }

            const int targetFrameBytes = qBound(8, bitrate / 400, 512);
            if (m_frameBytes != targetFrameBytes)
            {
                m_frameBytes = targetFrameBytes;
                emit frameBytesChanged();
            }
            if (m_pcmFrameBytes != 320)
            {
                m_pcmFrameBytes = 320;
                emit pcmFrameBytesChanged();
            }
            if (m_frameMs != 20)
            {
                m_frameMs = 20;
                emit frameMsChanged();
            }
            if (!m_opusActive)
            {
                m_opusActive = true;
                emit opusActiveChanged();
            }
            if (m_codec2Active)
            {
                m_codec2Active = false;
                emit codec2ActiveChanged();
            }
            return;
        }

        logCodec2Status("Opus init failed encErr=%d decErr=%d (requested bitrate=%d path=%s loaded=%d).",
                        encErr,
                        decErr,
                        m_mode,
                        m_opusLibraryPath.toUtf8().constData(),
                        m_opusLibraryLoaded ? 1 : 0);
        if (m_opusEncoder)
        {
            if (m_opusUsingRuntimeApi && m_opusEncoderDestroy)
                m_opusEncoderDestroy(m_opusEncoder);
            else
                opus_encoder_destroy(m_opusEncoder);
            m_opusEncoder = nullptr;
        }
        if (m_opusDecoder)
        {
            if (m_opusUsingRuntimeApi && m_opusDecoderDestroy)
                m_opusDecoderDestroy(m_opusDecoder);
            else
                opus_decoder_destroy(m_opusDecoder);
            m_opusDecoder = nullptr;
        }
        m_opusUsingRuntimeApi = false;
    }
#endif

#ifdef INCOMUDON_USE_CODEC2
    int codecMode = kCodec2Mode1600;
    int requestedCodec2Bitrate = m_mode;
    if (requestCodec2)
    {
        switch (requestedCodec2Bitrate)
        {
        case 450:
            codecMode = kCodec2Mode450;
            break;
        case 700:
            codecMode = kCodec2Mode700C;
            break;
        case 2400:
            codecMode = kCodec2Mode2400;
            break;
        case 3200:
            codecMode = kCodec2Mode3200;
            break;
        default:
            codecMode = kCodec2Mode1600;
            break;
        }
    }

    if (requestCodec2 && !m_codec2LibraryLoaded)
        refreshCodec2Library();

    if (requestCodec2 && m_codec2LibraryLoaded &&
        m_codec2Create && m_codec2Encode && m_codec2Decode &&
        m_codec2BitsPerFrame && m_codec2SamplesPerFrame)
    {
        int bitsPerFrame = 0;
        int samples = 0;
        bool createOk = true;
        {
            QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
            createOk = runGuardedCodec2InitCall("codec2_create", [&]() {
                m_codec = m_codec2Create(codecMode);
                if (m_codec)
                {
                    bitsPerFrame = m_codec2BitsPerFrame(m_codec);
                    samples = m_codec2SamplesPerFrame(m_codec);
                }
            });
        }

        if (!createOk)
        {
            m_codec = nullptr;
            const QString badPath = normalizeLibraryPath(m_codec2LibraryPath);
            if (!badPath.isEmpty())
                m_codec2KnownBadPath = badPath;
            clearCodec2Api();
            unloadCodec2Library();
            setCodec2LibraryLoadedInternal(false);
            setCodec2LibraryErrorInternal(
                QStringLiteral("codec2 library crashed during initialization (incompatible build)"));
        }
        if (m_codec)
        {
            bool encodeProbeOk = true;
            bool encodeProducedNonZero = false;
            bool decodeProducedNonZero = false;
            m_codec2EncodeActive = m_codec2Encode;
            m_codec2DecodeActive = m_codec2Decode;
            if (bitsPerFrame > 0 && bitsPerFrame <= 4096 &&
                samples > 0 && samples <= 4096)
            {
                auto hasNonZeroByte = [](const QByteArray& data) {
                    for (char b : data)
                    {
                        if (b != 0)
                            return true;
                    }
                    return false;
                };

                auto runEncodeProbe = [&](Codec2EncodeFn encodeFn,
                                          const char* probeName,
                                          bool* producedNonZero,
                                          QByteArray* sampleBits) {
                    bool localOk = true;
                    bool localNonZero = false;
                    if (!encodeFn)
                        return false;

                    for (int probeIdx = 0; probeIdx < 2 && localOk; ++probeIdx)
                    {
                        QByteArray probeBits((bitsPerFrame + 7) / 8, 0);
                        QVector<short> probeSamples(samples, 0);
                        quint32 prng = 0x9e3779b9u ^ static_cast<quint32>(probeIdx * 0x85ebca6bu);
                        for (int i = 0; i < samples; ++i)
                        {
                            // Use a deterministic pseudo-random voiced-like probe.
                            // Some codec2 builds may quantize simple periodic probes to near-zero.
                            prng = prng * 1664525u + 1013904223u + static_cast<quint32>(i * 17u + probeIdx);
                            const int noise = static_cast<int>((prng >> 16) & 0x7fff) - 16384;
                            const int saw = ((i * 521 + probeIdx * 997) % 32768) - 16384;
                            int mixed = (noise / 2) + (saw / 2);
                            if (mixed > 32767)
                                mixed = 32767;
                            if (mixed < -32768)
                                mixed = -32768;
                            probeSamples[i] = static_cast<short>(mixed);
                        }

                        {
                            QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
                            localOk = runGuardedCodec2InitCall(probeName, [&]() {
                                encodeFn(m_codec,
                                         reinterpret_cast<unsigned char*>(probeBits.data()),
                                         probeSamples.data());
                            });
                        }

                        if (sampleBits && localOk && sampleBits->isEmpty())
                            *sampleBits = probeBits;

                        if (localOk && hasNonZeroByte(probeBits))
                        {
                            localNonZero = true;
                            if (sampleBits)
                                *sampleBits = probeBits;
                        }
                    }
                    if (producedNonZero)
                        *producedNonZero = localNonZero;
                    return localOk;
                };

                auto runDecodeProbe = [&](Codec2DecodeFn decodeFn,
                                          const char* probeName,
                                          const QByteArray& bitsFrame,
                                          bool* producedNonZero) {
                    bool localOk = true;
                    bool localNonZero = false;
                    if (!decodeFn || bitsFrame.isEmpty())
                        return false;

                    QVector<short> decodedSamples(samples, 0);
                    {
                        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
                        localOk = runGuardedCodec2InitCall(probeName, [&]() {
                            decodeFn(m_codec,
                                     decodedSamples.data(),
                                     reinterpret_cast<const unsigned char*>(bitsFrame.constData()));
                        });
                    }

                    if (localOk)
                    {
                        for (short s : decodedSamples)
                        {
                            if (s != 0)
                            {
                                localNonZero = true;
                                break;
                            }
                        }
                    }

                    if (producedNonZero)
                        *producedNonZero = localNonZero;
                    return localOk;
                };

                QByteArray genericProbeBits;
                const bool genericProbeOk = runEncodeProbe(m_codec2EncodeActive,
                                                           "codec2_encode_probe",
                                                           &encodeProducedNonZero,
                                                           &genericProbeBits);
                encodeProbeOk = genericProbeOk;
                bool decodeProbeOk = false;
                decodeProducedNonZero = false;
                if (genericProbeOk)
                {
                    decodeProbeOk = runDecodeProbe(m_codec2DecodeActive,
                                                   "codec2_decode_probe",
                                                   genericProbeBits,
                                                   &decodeProducedNonZero);
                }

                logCodec2Status("codec2 generic probes mode=%d bitrate=%d encodeOk=%d encodeNonZero=%d decodeOk=%d decodeNonZero=%d",
                                codecMode,
                                requestedCodec2Bitrate,
                                genericProbeOk ? 1 : 0,
                                encodeProducedNonZero ? 1 : 0,
                                decodeProbeOk ? 1 : 0,
                                decodeProducedNonZero ? 1 : 0);

                // Mode-specific entry points are only attempted when generic API calls
                // themselves crash. If generic calls are stable but return silent data,
                // treat the library as incompatible instead of calling mode-specific paths
                // that are known to crash on some third-party builds.
                if (!genericProbeOk || !decodeProbeOk)
                {
                    const auto resolveCodec2Symbol = [&](const char* name) -> void* {
                        if (!name || name[0] == '\0')
                            return nullptr;
#if defined(Q_OS_ANDROID)
                        if (m_codec2DlHandle)
                            return dlsym(m_codec2DlHandle, name);
                        return nullptr;
#else
                        if (m_codec2Library)
                            return m_codec2Library->resolve(name);
                        return nullptr;
#endif
                    };

                    const char* modeEncodeName = nullptr;
                    const char* modeDecodeName = nullptr;
                    switch (codecMode)
                    {
                    case kCodec2Mode3200:
                        modeEncodeName = "codec2_encode_3200";
                        modeDecodeName = "codec2_decode_3200";
                        break;
                    case kCodec2Mode2400:
                        modeEncodeName = "codec2_encode_2400";
                        modeDecodeName = "codec2_decode_2400";
                        break;
                    case kCodec2Mode1600:
                        modeEncodeName = "codec2_encode_1600";
                        modeDecodeName = "codec2_decode_1600";
                        break;
                    case kCodec2Mode700C:
                        modeEncodeName = "codec2_encode_700c";
                        modeDecodeName = "codec2_decode_700c";
                        break;
                    case kCodec2Mode450:
                        modeEncodeName = "codec2_encode_450";
                        modeDecodeName = "codec2_decode_450";
                        break;
                    default:
                        break;
                    }

                    Codec2EncodeFn modeEncodeFn = reinterpret_cast<Codec2EncodeFn>(
                        resolveCodec2Symbol(modeEncodeName));
                    Codec2DecodeFn modeDecodeFn = reinterpret_cast<Codec2DecodeFn>(
                        resolveCodec2Symbol(modeDecodeName));
                    logCodec2Status("codec2 mode-specific symbols mode=%d encode=%p decode=%p",
                                    codecMode,
                                    reinterpret_cast<void*>(modeEncodeFn),
                                    reinterpret_cast<void*>(modeDecodeFn));

                    if (modeEncodeFn && modeDecodeFn)
                    {
                        QByteArray modeProbeBits;
                        bool modeProducedNonZero = false;
                        const bool modeProbeOk = runEncodeProbe(modeEncodeFn,
                                                                "codec2_encode_mode_probe",
                                                                &modeProducedNonZero,
                                                                &modeProbeBits);
                        bool modeDecodeProducedNonZero = false;
                        const bool modeDecodeOk = modeProbeOk &&
                                                  runDecodeProbe(modeDecodeFn,
                                                                 "codec2_decode_mode_probe",
                                                                 modeProbeBits,
                                                                 &modeDecodeProducedNonZero);
                        if (modeProbeOk)
                        {
                            m_codec2EncodeActive = modeEncodeFn;
                            m_codec2DecodeActive = modeDecodeFn;
                            encodeProbeOk = modeDecodeOk;
                            encodeProducedNonZero = modeProducedNonZero;
                            decodeProducedNonZero = modeDecodeProducedNonZero;
                            if (modeDecodeOk)
                            {
                                logCodec2Status("codec2 using mode-specific api encode=%s decode=%s mode=%d.",
                                                modeEncodeName, modeDecodeName, codecMode);
                            }
                        }
                        else
                        {
                            encodeProbeOk = modeProbeOk;
                            encodeProducedNonZero = modeProducedNonZero;
                        }
                    }
                }
            }

            if (encodeProbeOk && (!encodeProducedNonZero || !decodeProducedNonZero))
            {
                logCodec2Status(
                    "codec2 probe produced silent payload mode=%d bitrate=%d (encodeNonZero=%d decodeNonZero=%d).",
                    codecMode,
                    requestedCodec2Bitrate,
                    encodeProducedNonZero ? 1 : 0,
                    decodeProducedNonZero ? 1 : 0);
                encodeProbeOk = false;
            }

            if (encodeProbeOk)
            {
                // Probes exercise codec state with synthetic data.
                // Recreate the codec so real TX/RX starts from a clean state.
                bool resetOk = true;
                {
                    QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
                    resetOk = runGuardedCodec2InitCall("codec2_destroy_after_probe_pass", [&]() {
                        if (m_codec2Destroy && m_codec)
                            m_codec2Destroy(m_codec);
                        m_codec = nullptr;
                    });
                    if (resetOk)
                    {
                        resetOk = runGuardedCodec2InitCall("codec2_recreate_after_probe_pass", [&]() {
                            m_codec = m_codec2Create(codecMode);
                            if (m_codec)
                            {
                                bitsPerFrame = m_codec2BitsPerFrame(m_codec);
                                samples = m_codec2SamplesPerFrame(m_codec);
                            }
                        });
                    }
                }
                if (!resetOk || !m_codec)
                {
                    encodeProbeOk = false;
                    logCodec2Status("codec2 recreate after probe failed mode=%d bitrate=%d.",
                                    codecMode, requestedCodec2Bitrate);
                }
                else
                {
                    logCodec2Status("codec2 state reset after probe mode=%d bits=%d samples=%d",
                                    codecMode, bitsPerFrame, samples);
                }
            }

            if (!encodeProbeOk)
            {
                m_codec2EncodeActive = nullptr;
                m_codec2DecodeActive = nullptr;
                logCodec2Status("codec2 encode probe failed mode=%d bitrate=%d.",
                                codecMode, requestedCodec2Bitrate);
                if (m_codec2Destroy)
                {
                    QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
                    runGuardedCodec2InitCall("codec2_destroy_after_probe_fail", [&]() {
                        m_codec2Destroy(m_codec);
                    });
                    m_codec = nullptr;
                }
                const QString badPath = normalizeLibraryPath(m_codec2LibraryPath);
                if (!badPath.isEmpty())
                    m_codec2KnownBadPath = badPath;
                clearCodec2Api();
                unloadCodec2Library();
                setCodec2LibraryLoadedInternal(false);
                setCodec2LibraryErrorInternal(
                    QStringLiteral("codec2 encode probe failed (incompatible build)"));
            }
        }
        if (m_codec)
        {
            if (bitsPerFrame <= 0 || bitsPerFrame > 4096 ||
                samples <= 0 || samples > 4096)
            {
                logCodec2Status("codec2 returned invalid frame params bits=%d samples=%d mode=%d.",
                                bitsPerFrame, samples, m_mode);
                if (m_codec2Destroy)
                {
                    QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
                    m_codec2Destroy(m_codec);
                    m_codec = nullptr;
                }
            }
            else
            {
                const int newFrameBytes = (bitsPerFrame + 7) / 8;
                const int newPcmBytes = samples * static_cast<int>(sizeof(short));
                const int newFrameMs = (samples * 1000) / 8000;
                if (newFrameBytes <= 0 || newPcmBytes <= 0)
                {
                    logCodec2Status("codec2 produced zero-sized frames bytes=%d pcm=%d mode=%d.",
                                    newFrameBytes, newPcmBytes, m_mode);
                    if (m_codec2Destroy)
                    {
                        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
                        m_codec2Destroy(m_codec);
                        m_codec = nullptr;
                    }
                }
                else
                {
                    if (!m_codec2Active)
                    {
                        m_codec2Active = true;
                        emit codec2ActiveChanged();
                    }
                    if (m_opusActive)
                    {
                        m_opusActive = false;
                        emit opusActiveChanged();
                    }

                    if (m_frameBytes != newFrameBytes)
                    {
                        m_frameBytes = newFrameBytes;
                        emit frameBytesChanged();
                    }
                    if (m_pcmFrameBytes != newPcmBytes)
                    {
                        m_pcmFrameBytes = newPcmBytes;
                        emit pcmFrameBytesChanged();
                    }
                    if (m_frameMs != newFrameMs && newFrameMs > 0)
                    {
                        m_frameMs = newFrameMs;
                        emit frameMsChanged();
                    }
                    return;
                }
            }
        }
        logCodec2Status("codec2_create failed for mode=%d (requested bitrate=%d).",
                        codecMode, requestedCodec2Bitrate);
    }

    if (requestCodec2)
    {
        if (!m_codec2LibraryError.isEmpty())
            logCodec2Status("Falling back to PCM: %s", m_codec2LibraryError.toUtf8().constData());
        else
            logCodec2Status("Falling back to PCM mode=%d (requested bitrate=%d).",
                            codecMode, requestedCodec2Bitrate);
    }
#endif

#ifndef INCOMUDON_USE_OPUS
    if (requestOpus)
        logCodec2Status("Falling back to PCM: Opus support disabled at build time.");
#endif

    if (m_codec2Active)
    {
        m_codec2Active = false;
        emit codec2ActiveChanged();
    }
    if (m_opusActive)
    {
        m_opusActive = false;
        emit opusActiveChanged();
    }
    const int fallbackPcm = 320;
    if (m_pcmFrameBytes != fallbackPcm)
    {
        m_pcmFrameBytes = fallbackPcm;
        emit pcmFrameBytesChanged();
    }
    if (m_frameBytes != m_pcmFrameBytes)
    {
        m_frameBytes = m_pcmFrameBytes;
        emit frameBytesChanged();
    }
    if (m_frameMs != 20)
    {
        m_frameMs = 20;
        emit frameMsChanged();
    }
}

int Codec2Wrapper::normalizeMode(int mode) const
{
    const int options[] = {450, 700, 1600, 2400, 3200};
    int best = options[0];
    int bestDiff = qAbs(mode - options[0]);
    for (int i = 1; i < 5; ++i)
    {
        const int diff = qAbs(mode - options[i]);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            best = options[i];
        }
    }
    return best;
}

int Codec2Wrapper::opusBitrateForMode(int mode) const
{
    if (mode < 6000)
        return 6000;

    static constexpr int options[] = {6000, 8000, 12000, 16000, 20000, 64000, 96000, 128000};
    int best = options[0];
    int bestDiff = qAbs(mode - options[0]);
    for (int i = 1; i < 8; ++i)
    {
        const int diff = qAbs(mode - options[i]);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            best = options[i];
        }
    }
    return best;
}

#ifdef INCOMUDON_USE_CODEC2
void Codec2Wrapper::unloadCodec2Library()
{
#if defined(Q_OS_ANDROID)
    if (m_codec2DlHandle)
    {
        dlclose(m_codec2DlHandle);
        m_codec2DlHandle = nullptr;
    }
#endif
    if (!m_codec2Library)
        return;
    if (m_codec2Library->isLoaded())
        m_codec2Library->unload();
}

void Codec2Wrapper::clearCodec2Api()
{
    m_codec2Create = nullptr;
    m_codec2Destroy = nullptr;
    m_codec2Encode = nullptr;
    m_codec2Decode = nullptr;
    m_codec2EncodeActive = nullptr;
    m_codec2DecodeActive = nullptr;
    m_codec2BitsPerFrame = nullptr;
    m_codec2SamplesPerFrame = nullptr;
    m_codec2AbiVersion = nullptr;
}

QString Codec2Wrapper::normalizeLibraryPath(const QString& path, QString* error) const
{
    return normalizeDynamicLibraryPath(path, error);
}

void Codec2Wrapper::setCodec2LibraryLoadedInternal(bool loaded)
{
    if (m_codec2LibraryLoaded == loaded)
        return;
    m_codec2LibraryLoaded = loaded;
    emit codec2LibraryLoadedChanged();
}

void Codec2Wrapper::setCodec2LibraryErrorInternal(const QString& error)
{
    if (m_codec2LibraryError == error)
        return;
    m_codec2LibraryError = error;
    emit codec2LibraryErrorChanged();
}

void Codec2Wrapper::refreshCodec2Library()
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (!m_codec2Library)
        return;

    if (m_codec && m_codec2Destroy)
    {
        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
        m_codec2Destroy(m_codec);
        m_codec = nullptr;
    }

    clearCodec2Api();
    unloadCodec2Library();

    const bool explicitPath = !m_codec2LibraryPath.trimmed().isEmpty();
    QString normalizedError;
    const QString normalizedPath = normalizeLibraryPath(m_codec2LibraryPath, &normalizedError);
    if (explicitPath && normalizedPath.isEmpty())
    {
        setCodec2LibraryLoadedInternal(false);
        if (normalizedError.isEmpty())
            normalizedError = QStringLiteral("Invalid codec2 library path");
        setCodec2LibraryErrorInternal(normalizedError);
        return;
    }
    if (explicitPath &&
        !m_codec2KnownBadPath.isEmpty() &&
        normalizedPath == m_codec2KnownBadPath)
    {
        setCodec2LibraryLoadedInternal(false);
        setCodec2LibraryErrorInternal(
            QStringLiteral("codec2 library crashed during initialization (incompatible build)"));
        return;
    }
    QStringList candidates;
    if (explicitPath)
    {
        candidates << normalizedPath;
    }
    else
    {
#if defined(INCOMUDON_CODEC2_RUNTIME_LOADER) && defined(Q_OS_ANDROID)
        setCodec2LibraryLoadedInternal(false);
        setCodec2LibraryErrorInternal(QStringLiteral("Codec2 library path is not set"));
        return;
#else
        candidates << QStringLiteral("codec2") << QStringLiteral("libcodec2");
#endif
    }

    QString lastError;
    for (const QString& candidate : candidates)
    {
#if defined(Q_OS_ANDROID)
        QByteArray candidateBytes = QFile::encodeName(candidate);
        dlerror();
        m_codec2DlHandle = dlopen(candidateBytes.constData(), RTLD_NOW | RTLD_GLOBAL);
        if (!m_codec2DlHandle)
        {
            const char* globalErr = dlerror();
            const QString globalErrText = globalErr
                                              ? QString::fromUtf8(globalErr)
                                              : QStringLiteral("dlopen failed (RTLD_NOW|RTLD_GLOBAL)");
            dlerror();
            m_codec2DlHandle = dlopen(candidateBytes.constData(), RTLD_NOW | RTLD_LOCAL);
            if (!m_codec2DlHandle)
            {
                const char* localErr = dlerror();
                const QString localErrText = localErr
                                                 ? QString::fromUtf8(localErr)
                                                 : QStringLiteral("dlopen failed (RTLD_NOW|RTLD_LOCAL)");
                lastError = QStringLiteral("RTLD_GLOBAL: %1 / RTLD_LOCAL: %2")
                                .arg(globalErrText, localErrText);
                continue;
            }
        }
#else
        m_codec2Library->setFileName(candidate);
        if (!m_codec2Library->load())
        {
            lastError = m_codec2Library->errorString();
            continue;
        }
#endif
#if defined(Q_OS_ANDROID)
        m_codec2Create = reinterpret_cast<Codec2CreateFn>(dlsym(m_codec2DlHandle, "codec2_create"));
        m_codec2Destroy = reinterpret_cast<Codec2DestroyFn>(dlsym(m_codec2DlHandle, "codec2_destroy"));
        m_codec2Encode = reinterpret_cast<Codec2EncodeFn>(dlsym(m_codec2DlHandle, "codec2_encode"));
        m_codec2Decode = reinterpret_cast<Codec2DecodeFn>(dlsym(m_codec2DlHandle, "codec2_decode"));
        m_codec2BitsPerFrame = reinterpret_cast<Codec2BitsPerFrameFn>(dlsym(m_codec2DlHandle, "codec2_bits_per_frame"));
        m_codec2SamplesPerFrame = reinterpret_cast<Codec2SamplesPerFrameFn>(dlsym(m_codec2DlHandle, "codec2_samples_per_frame"));
        m_codec2AbiVersion = reinterpret_cast<Codec2AbiVersionFn>(dlsym(m_codec2DlHandle, "incomudon_codec2_abi_version"));
#else
        m_codec2Create = reinterpret_cast<Codec2CreateFn>(m_codec2Library->resolve("codec2_create"));
        m_codec2Destroy = reinterpret_cast<Codec2DestroyFn>(m_codec2Library->resolve("codec2_destroy"));
        m_codec2Encode = reinterpret_cast<Codec2EncodeFn>(m_codec2Library->resolve("codec2_encode"));
        m_codec2Decode = reinterpret_cast<Codec2DecodeFn>(m_codec2Library->resolve("codec2_decode"));
        m_codec2BitsPerFrame = reinterpret_cast<Codec2BitsPerFrameFn>(m_codec2Library->resolve("codec2_bits_per_frame"));
        m_codec2SamplesPerFrame = reinterpret_cast<Codec2SamplesPerFrameFn>(m_codec2Library->resolve("codec2_samples_per_frame"));
        m_codec2AbiVersion = reinterpret_cast<Codec2AbiVersionFn>(m_codec2Library->resolve("incomudon_codec2_abi_version"));
#endif
        m_codec2EncodeActive = m_codec2Encode;
        m_codec2DecodeActive = m_codec2Decode;

#if defined(Q_OS_ANDROID)
        const QFileInfo candidateInfo(candidate);
        logCodec2Status("Codec2 candidate loaded: %s (size=%lld)",
                        candidate.toUtf8().constData(),
                        static_cast<long long>(candidateInfo.size()));
        logCodec2Status("Codec2 symbols create=%p destroy=%p abi=%p",
                        reinterpret_cast<void*>(m_codec2Create),
                        reinterpret_cast<void*>(m_codec2Destroy),
                        reinterpret_cast<void*>(m_codec2AbiVersion));
        if (m_codec2Create)
        {
            Dl_info dlinfo {};
            if (dladdr(reinterpret_cast<void*>(m_codec2Create), &dlinfo) != 0 &&
                dlinfo.dli_fname)
            {
                logCodec2Status("Codec2 dladdr create: base=%p sym=%p file=%s",
                                dlinfo.dli_fbase,
                                dlinfo.dli_saddr,
                                dlinfo.dli_fname);

#if defined(INCOMUDON_USE_CODEC2)
                QString patchError;
                int patchedSlots = 0;
                if (patchAndroidJumpSlotsFromFile(candidate, dlinfo.dli_fbase, &patchError, &patchedSlots))
                {
                    if (patchedSlots > 0)
                    {
                        logCodec2Status("Codec2 relocation workaround applied: patched=%d",
                                        patchedSlots);
                    }
                }
                else if (!patchError.isEmpty())
                {
                    logCodec2Status("Codec2 relocation workaround skipped: %s",
                                    patchError.toUtf8().constData());
                }
#endif
            }
        }
#endif

        const bool symbolsOk = (m_codec2Create && m_codec2Destroy &&
                                m_codec2Encode && m_codec2Decode &&
                                m_codec2BitsPerFrame && m_codec2SamplesPerFrame &&
                                m_codec2AbiVersion);
        if (symbolsOk)
        {
            const int abiVersion = m_codec2AbiVersion();
            if (abiVersion == kIncomUdonCodec2AbiVersion)
            {
                CODEC2* probeCodec = nullptr;
                const bool probeOk = runGuardedCodec2InitCall("codec2_create_probe", [&]() {
                    probeCodec = m_codec2Create(kCodec2Mode1600);
                });
                if (!probeOk || probeCodec == nullptr)
                {
                    clearCodec2Api();
                    unloadCodec2Library();
                    if (explicitPath)
                        m_codec2KnownBadPath = normalizedPath;
                    lastError = QStringLiteral(
                        "codec2 library crashed during probe (incompatible build)");
                    continue;
                }
                runGuardedCodec2InitCall("codec2_destroy_probe", [&]() {
                    m_codec2Destroy(probeCodec);
                });

                setCodec2LibraryLoadedInternal(true);
                setCodec2LibraryErrorInternal(QString());
                return;
            }

            clearCodec2Api();
            unloadCodec2Library();
            if (explicitPath)
                m_codec2KnownBadPath = normalizedPath;
            lastError = QStringLiteral(
                "codec2 ABI mismatch (expected %1, got %2)")
                    .arg(kIncomUdonCodec2AbiVersion)
                    .arg(abiVersion);
            continue;
        }

        lastError = QStringLiteral(
            "Required codec2 symbols were not found (including incomudon_codec2_abi_version): %1")
                        .arg(candidate);
        clearCodec2Api();
        unloadCodec2Library();
    }

    setCodec2LibraryLoadedInternal(false);
    if (explicitPath)
    {
        if (lastError.isEmpty())
            lastError = QStringLiteral("Failed to load codec2 library: %1").arg(normalizedPath);
        setCodec2LibraryErrorInternal(lastError);
    }
    else
    {
        setCodec2LibraryErrorInternal(QString());
    }
}
#endif
