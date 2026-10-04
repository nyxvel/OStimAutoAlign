#pragma once

// OStim Standalone's thread API (OStim.dll export RequestPluginAPI_Thread), declared here only up to
// the alignment functions. The virtual functions must stay in OStim's order: the vtable slots after
// SetActorAlignment are not called, so they are left out. Source: OStimNG 7.5.1.2,
// skse/src/ModAPI/OstimNG-API-Thread.h (github.com/VersuchDrei/OStimNG).

namespace OStimAPI
{
	enum class InterfaceVersion : std::uint8_t
	{
		V1
	};

	enum class APIResult : std::uint8_t
	{
		OK,
		Invalid,
		Failed
	};

	struct ActorData
	{
		std::uint32_t formID;
		float         excitement;
		bool          isFemale;
		bool          hasSchlong;
		std::int32_t  timesClimaxed;
	};

	struct ActorAlignmentData
	{
		float offsetX;   // to the thread center's right
		float offsetY;   // forward
		float offsetZ;
		float scale;     // multiplier
		float rotation;  // degrees
		float sosBend;   // SOSBend event number, -10 = SOSFlaccid
	};

	using ThreadEventCallback = void (*)(std::uint8_t a_event, std::uint32_t a_threadID, void* a_userData);
	using ControlEventCallback = void (*)(std::uint8_t a_control, std::uint32_t a_threadID, void* a_userData);

	class IThreadInterface
	{
	public:
		virtual std::uint32_t GetPlayerThreadID() noexcept = 0;
		virtual bool          IsThreadValid(std::uint32_t a_threadID) noexcept = 0;
		virtual const char*   GetCurrentSceneID(std::uint32_t a_threadID) noexcept = 0;
		virtual std::uint32_t GetActorCount(std::uint32_t a_threadID) noexcept = 0;
		virtual std::uint32_t GetActors(std::uint32_t a_threadID, ActorData* a_buffer, std::uint32_t a_bufferSize) noexcept = 0;
		virtual std::uint32_t GetNavigationCount(std::uint32_t a_threadID) noexcept = 0;
		virtual std::uint32_t GetNavigationOptions(std::uint32_t a_threadID, void* a_buffer, std::uint32_t a_bufferSize) noexcept = 0;
		virtual APIResult     NavigateToScene(std::uint32_t a_threadID, const char* a_sceneID) noexcept = 0;
		virtual bool          IsTransition(std::uint32_t a_threadID) noexcept = 0;
		virtual bool          IsInSequence(std::uint32_t a_threadID) noexcept = 0;
		virtual bool          IsAutoMode(std::uint32_t a_threadID) noexcept = 0;
		virtual bool          IsPlayerControlDisabled(std::uint32_t a_threadID) noexcept = 0;
		virtual void          RegisterEventCallback(ThreadEventCallback a_callback, void* a_userData) noexcept = 0;
		virtual void          UnregisterEventCallback(ThreadEventCallback a_callback) noexcept = 0;
		virtual void          RegisterControlCallback(ControlEventCallback a_callback, void* a_userData) noexcept = 0;
		virtual void          UnregisterControlCallback(ControlEventCallback a_callback) noexcept = 0;
		virtual void          SetExternalUIEnabled(bool a_enabled) noexcept = 0;
		virtual void          GetKeyData(void* a_outData) noexcept = 0;
		virtual const char*   GetCurrentNodeName(std::uint32_t a_threadID) noexcept = 0;
		virtual std::int32_t  GetCurrentSpeed(std::uint32_t a_threadID) noexcept = 0;
		virtual std::int32_t  GetMaxSpeed(std::uint32_t a_threadID) noexcept = 0;
		virtual APIResult     SetSpeed(std::uint32_t a_threadID, std::int32_t a_speed) noexcept = 0;
		virtual bool          GetActorAlignment(std::uint32_t a_threadID, std::uint32_t a_actorIndex, ActorAlignmentData* a_outData) noexcept = 0;
		virtual APIResult     SetActorAlignment(std::uint32_t a_threadID, std::uint32_t a_actorIndex, const ActorAlignmentData* a_data) noexcept = 0;
	};

	// nullptr when OStim.dll isn't loaded or has no thread API (OStim < 7.5)
	inline IThreadInterface* Get()
	{
		static IThreadInterface* api = []() -> IThreadInterface* {
			const auto module = REX::W32::GetModuleHandleA("OStim.dll");
			if (!module) {
				return nullptr;
			}
			using Request = IThreadInterface* (*)(InterfaceVersion, const char*, REL::Version);
			const auto request = reinterpret_cast<Request>(REX::W32::GetProcAddress(module, "RequestPluginAPI_Thread"));
			return request ? request(InterfaceVersion::V1, "OStimAutoAlign", REL::Version{ 1, 0, 0, 0 }) : nullptr;
		}();
		return api;
	}
}
