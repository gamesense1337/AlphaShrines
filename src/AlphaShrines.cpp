#include "cwsdk.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <windows.h>

namespace
{
    constexpr size_t RespawnOffset = 0x2C2860;
    constexpr size_t SpawnListOffset = 0x2E6850;
    constexpr size_t ShrineCallerOffset = 0x2F1895;

    constexpr size_t RespawnBytes = 16;
    constexpr size_t SpawnListBytes = 20;
    constexpr uint32_t ShrineRecord = 0x0C;
    constexpr int32_t CellSize = 256;
    constexpr int64_t FixedScale = 65536;

    struct LongVector3
    {
        int64_t X;
        int64_t Y;
        int64_t Z;
    };

    struct LifeShrine
    {
        int32_t X;
        int32_t Y;
        int32_t Z;
    };

    using RespawnFn = LongVector3* (__fastcall*)(void* Controller, LongVector3* Out, const LongVector3* Position);
    using SpawnListFn = void* (__fastcall*)(void* List, void* Last, void* Parent, const void* Spawn);

    RespawnFn OriginalRespawn = nullptr;
    SpawnListFn OriginalSpawnList = nullptr;
    SRWLOCK LifeShrineLock = SRWLOCK_INIT;
    std::vector<LifeShrine> LifeShrines;

    LongVector3* __fastcall RespawnHook(void* Controller, LongVector3* Out, const LongVector3* Position);
    void* __fastcall SpawnListHook(void* List, void* Last, void* Parent, const void* Spawn);

    void PrintLoadMessage(cube::Game* Game, bool Hooks)
    {
        if (!Game)
        {
            return;
        }

        const wchar_t* const Status = Hooks ? L" Alpha logic restored.\n" : L" This Cube World build was not recognized.\n";

        Game->PrintMessage(L"[AlphaShrines]", static_cast<char>(0x19), static_cast<char>(0x33), static_cast<char>(0x8C));

        cube::ChatWidget* const ChatWidget = Game->gui.chat_widget;
        if (!ChatWidget || ChatWidget->message_sections.empty() || ChatWidget->message_sections.back().empty())
        {
            Game->PrintMessage(Status, static_cast<char>(0xFF), static_cast<char>(0xFF), static_cast<char>(0xFF));
            return;
        }

        cube::ChatWidget::MessageData TextSection = ChatWidget->message_sections.back().back();
        TextSection.text = Status;
        TextSection.color = ByteRGBA(static_cast<unsigned char>(0xFF), static_cast<unsigned char>(0xFF), static_cast<unsigned char>(0xFF), static_cast<unsigned char>(0xFF));
        ChatWidget->message_sections.back().push_back(TextSection);
    }

    void WriteJump(uint8_t* Source, const void* Destination)
    {
        Source[0] = 0xFF;
        Source[1] = 0x25;
        *reinterpret_cast<uint32_t*>(Source + 2) = 0;
        *reinterpret_cast<uint64_t*>(Source + 6) = reinterpret_cast<uint64_t>(Destination);
    }

    bool MakeTrampoline(uint8_t* Target, size_t PatchLength, void** Original)
    {
        auto* const Trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, PatchLength + 14, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!Trampoline)
        {
            return false;
        }

        std::memcpy(Trampoline, Target, PatchLength);
        WriteJump(Trampoline + PatchLength, Target + PatchLength);
        FlushInstructionCache(GetCurrentProcess(), Trampoline, PatchLength + 14);
        *Original = Trampoline;
        return true;
    }

    bool VerifyOffsets()
    {
        constexpr std::array<uint8_t, RespawnBytes> RespawnPrologue =
        {
            0x48, 0x8B, 0xC4, 0x53, 0x55, 0x56, 0x57, 0x41,
            0x54, 0x48, 0x81, 0xEC, 0xE0, 0x00, 0x00, 0x00
        };
        constexpr std::array<uint8_t, SpawnListBytes> SpawnListPrologue =
        {
            0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7,
            0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
            0x89, 0x5C, 0x24, 0x48
        };

        return std::memcmp(CWOffset(RespawnOffset), RespawnPrologue.data(), RespawnPrologue.size()) == 0 &&
            std::memcmp(CWOffset(SpawnListOffset), SpawnListPrologue.data(), SpawnListPrologue.size()) == 0;
    }

    bool SetupHooks()
    {
        if (!VerifyOffsets())
        {
            return false;
        }

        auto* const RespawnAddress = static_cast<uint8_t*>(CWOffset(RespawnOffset));
        auto* const SpawnListAddress = static_cast<uint8_t*>(CWOffset(SpawnListOffset));
        if (!MakeTrampoline(RespawnAddress, RespawnBytes, reinterpret_cast<void**>(&OriginalRespawn)) ||
            !MakeTrampoline(SpawnListAddress, SpawnListBytes, reinterpret_cast<void**>(&OriginalSpawnList)))
        {
            return false;
        }

        WriteFarJMP(RespawnAddress, reinterpret_cast<void*>(&RespawnHook));
        WriteFarJMP(SpawnListAddress, reinterpret_cast<void*>(&SpawnListHook));
        FlushInstructionCache(GetCurrentProcess(), RespawnAddress, RespawnBytes);
        FlushInstructionCache(GetCurrentProcess(), SpawnListAddress, SpawnListBytes);
        return true;
    }

    int64_t FloorDivide(int64_t Value, int64_t Divisor)
    {
        int64_t Result = Value / Divisor;
        if (Value < 0 && Value % Divisor != 0)
        {
            --Result;
        }
        return Result;
    }

    int64_t GetDistanceSqr(const LifeShrine& Statue, int64_t OriginX, int64_t OriginY)
    {
        const int64_t DeltaX = static_cast<int64_t>(Statue.X) - OriginX;
        const int64_t DeltaY = static_cast<int64_t>(Statue.Y) - OriginY;
        return DeltaX * DeltaX + DeltaY * DeltaY;
    }

    void AddLifeShrine(const LifeShrine& Statue)
    {
        AcquireSRWLockExclusive(&LifeShrineLock);
        const auto Existing = std::find_if(LifeShrines.begin(), LifeShrines.end(), [&Statue](const LifeShrine& Other)
        {
            return Other.X == Statue.X && Other.Y == Statue.Y && Other.Z == Statue.Z;
        });
        if (Existing == LifeShrines.end())
        {
            LifeShrines.push_back(Statue);
        }
        ReleaseSRWLockExclusive(&LifeShrineLock);
    }

    bool FindShrine(const LongVector3& Position, LifeShrine& Statue)
    {
        const int64_t OriginX = FloorDivide(Position.X, FixedScale);
        const int64_t OriginY = FloorDivide(Position.Y, FixedScale);
        const int64_t OriginCellX = FloorDivide(OriginX, CellSize);
        const int64_t OriginCellY = FloorDivide(OriginY, CellSize);

        std::vector<LifeShrine> Statues;
        AcquireSRWLockShared(&LifeShrineLock);
        Statues.reserve(LifeShrines.size());
        for (const LifeShrine& Other : LifeShrines)
        {
            const int64_t CellX = FloorDivide(Other.X, CellSize);
            const int64_t CellY = FloorDivide(Other.Y, CellSize);
            if (CellX >= OriginCellX - 1 && CellX <= OriginCellX + 1 && CellY >= OriginCellY - 1 && CellY <= OriginCellY + 1)
            {
                Statues.push_back(Other);
            }
        }
        ReleaseSRWLockShared(&LifeShrineLock);

        if (Statues.empty())
        {
            return false;
        }

        std::stable_sort(Statues.begin(), Statues.end(), [OriginX, OriginY](const LifeShrine& Left, const LifeShrine& Right)
        {
            return GetDistanceSqr(Left, OriginX, OriginY) < GetDistanceSqr(Right, OriginX, OriginY);
        });

        Statue = Statues[Statues.size() > 1 ? 1 : 0];
        return true;
    }

    bool ReadLifeShrine(const void* Spawn, uintptr_t Caller, LifeShrine& Statue)
    {
        if (!Spawn || Caller != reinterpret_cast<uintptr_t>(CWOffset(ShrineCallerOffset)))
        {
            return false;
        }

        __try
        {
            if (*reinterpret_cast<const uint32_t*>(Spawn) != ShrineRecord)
            {
                return false;
            }

            const auto* const Position = reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(Spawn) + sizeof(uint32_t));
            Statue = { Position[0], Position[1], Position[2] };
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* __fastcall SpawnListHook(void* List, void* Last, void* Parent, const void* Spawn)
    {
        LifeShrine Statue = {};
        const bool IsShrine = ReadLifeShrine(Spawn, reinterpret_cast<uintptr_t>(_ReturnAddress()), Statue);
        void* const Node = OriginalSpawnList ? OriginalSpawnList(List, Last, Parent, Spawn) : nullptr;

        if (IsShrine)
        {
            AddLifeShrine(Statue);
        }

        return Node;
    }

    LongVector3* __fastcall RespawnHook(void* Controller, LongVector3* Out, const LongVector3* Position)
    {
        LongVector3* const Result = OriginalRespawn ? OriginalRespawn(Controller, Out, Position) : Out;

        if (!Out || !Position)
        {
            return Result;
        }

        LifeShrine Statue = {};
        if (!FindShrine(*Position, Statue))
        {
            return Result;
        }

        Out->X = static_cast<int64_t>(Statue.X) * FixedScale;
        Out->Y = static_cast<int64_t>(Statue.Y) * FixedScale;
        Out->Z = static_cast<int64_t>(Statue.Z) * FixedScale;
        if (Result && Result != Out)
        {
            *Result = *Out;
        }
        return Out;
    }

    class AlphaShrines final : public GenericMod
    {
    public:
        void Initialize() override
        {
            OnGameTickPriority = HighPriority;
            Hooks = SetupHooks();
        }

        void OnGameTick(cube::Game* Game) override
        {
            if (!Game || Loaded)
            {
                return;
            }

            PrintLoadMessage(Game, Hooks);
            Loaded = true;
        }

    private:
        bool Loaded = false;
        bool Hooks = false;
    };
}

EXPORT GenericMod* MakeMod()
{
    return new AlphaShrines();
}
