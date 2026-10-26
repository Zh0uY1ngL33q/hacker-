#pragma once
//
// Runtime module dumper.
//
// The Minecraft.Windows.exe image is WinLicense packed: on disk the code section
// is ciphertext (entropy ~7.98), and is only decrypted into memory while the game
// runs. That makes offline signature work against the file on disk impossible, so
// we snapshot the *decrypted in-memory* image instead.
//
// Output is a PE-shaped virtual image (byte at file offset X == memory at RVA X)
// which pefile can parse directly, so all RVAs from the logs stay valid offline.
//
// Enabled by setting the environment variable SOLSTICE_DUMP=1 before injection.
//

#include <cstdint>
#include <string>
#include <vector>

class ModuleDump {
public:
    /// Dumps the decrypted image + a section manifest + a resolved-offset snapshot.
    /// Safe to call more than once; each call writes a new timestamped set of files.
    ///
    /// `force` bypasses the SOLSTICE_DUMP check. The signature-initialisation
    /// failure path uses it: the environment is captured when the game process
    /// starts, not when the DLL is injected, so requiring the variable to be set
    /// there means it is easy to end up with no image at exactly the moment the
    /// image is the only way to make progress.
    static bool dump(const std::string& moduleName = "Minecraft.Windows.exe",
                     bool force = false);

    /// True when SOLSTICE_DUMP is set to 1/true/yes (case insensitive).
    static bool isEnabled();

    /// Where dump artefacts go: SOLSTICE_DUMP_DIR when set, otherwise the
    /// client's Dump folder. Always ends with a separator. Public so everything
    /// written on a failing run -- image, manifest, sigreport.json -- lands in
    /// the same place instead of splitting across two directories.
    static std::string outputDir();

private:
    /// The actual dump. Wrapped by dump(), which turns any escaping exception into
    /// a log line: this runs on the client's init thread, where an exception that
    /// gets out terminates the process.
    static bool dumpImage(const std::string& moduleName);

    struct SectionInfo {
        std::string name;
        uint32_t    rva = 0;
        uint32_t    virtualSize = 0;
        uint32_t    rawSize = 0;
        uint32_t    characteristics = 0;
        uint32_t    dumpedBytes = 0;
        uint32_t    skippedBytes = 0;
        double      entropy = 0.0;
    };

    /// Copies [base+rva, base+rva+size) into out, zero-filling pages that are
    /// not committed/readable. `skipped` receives the number of bytes that had
    /// to be skipped, so callers can report gaps instead of mistaking them for
    /// real zero-filled code. Returns the number of bytes produced.
    static size_t copyReadable(uintptr_t base, uint32_t rva, uint32_t size,
                               std::vector<unsigned char>& out, uint32_t* skipped = nullptr);

    /// Shannon entropy of a byte buffer, 0.0 for empty input.
    static double entropy(const std::vector<unsigned char>& data);

    /// Writes the SigManager/OffsetProvider results as JSON so two game versions
    /// can be diffed to see exactly which offsets drifted.
    static void writeOffsetSnapshot(const std::string& path, const std::string& tag);
};
