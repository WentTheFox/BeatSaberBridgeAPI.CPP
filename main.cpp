#define DISCORDPP_IMPLEMENTATION
#include "include/discordpp.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include <csignal>
#include <queue>
#include <mutex>
#include <map>
#include <vector>
#include <set>
#include <ctime>
#include <chrono>
#include <memory>
#include <sstream>
#include <httplib.h>
#include <nlohmann/json.hpp>

// Replace with your Discord Application ID
const uint64_t APPLICATION_ID = 1492702030958170272;

// 15 minutes in seconds
const int INACTIVITY_TIMEOUT = 900;

// Create a flag to stop the application
std::atomic<bool> running = true;

// Event data structure
struct EventData {
    std::string type;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> mappers;
};

// Global state
std::queue<EventData> eventQueue;
std::mutex queueMutex;
std::string partyId = "";
EventData storedSongData;
time_t lastDataTime = std::time(nullptr);
bool rpcCleared = false;
time_t pauseStartTime = 0;
int totalPausedDuration = 0;

int httpPort = 8080;

// Signal handler to stop the application
void signalHandler(int signum) {
    running.store(false);
}

// Helper function to get current Unix timestamp
long long getCurrentTimestamp() {
    return std::chrono::system_clock::now().time_since_epoch().count() / 1000000000;
}

// Helper function to join mappers
std::string joinMappers(const std::vector<std::string>& mappers) {
    if (mappers.empty()) return "Unknown";
    
    std::set<std::string> uniqueMappers(mappers.begin(), mappers.end());
    std::string result;
    for (const auto& mapper : uniqueMappers) {
        if (!result.empty()) result += ", ";
        result += mapper;
    }
    return result;
}

void httpServer() {
    httplib::Server svr;

    svr.Post("/sendData", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto json = nlohmann::json::parse(req.body);
            
            EventData event;
            event.type = json["type"];
            
            // Extract metadata fields
            for (auto& [key, value] : json.items()) {
                if (key != "type" && key != "mappers") {
                    if (value.is_string()) {
                        event.metadata[key] = value.get<std::string>();
                    } else if (value.is_number()) {
                        event.metadata[key] = std::to_string(value.get<int>());
                    }
                }
            }
            
            // Extract mappers array
            if (json.contains("mappers") && json["mappers"].is_array()) {
                for (const auto& mapper : json["mappers"]) {
                    if (mapper.is_string()) {
                        event.mappers.push_back(mapper.get<std::string>());
                    }
                }
            }
            
            // Add event to queue
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                eventQueue.push(event);
            }
			
            std::cout << "📨 Received event: " << event.type << std::endl;
            
            res.set_content(nlohmann::json({{"status", "success"}}).dump(), "application/json");
            res.status = 200;
        } catch (const std::exception& e) {
            std::cerr << "❌ Error processing request: " << e.what() << std::endl;
            res.set_content(nlohmann::json({{"status", "error"}, {"message", e.what()}}).dump(), "application/json");
            res.status = 400;
        }
    });

    svr.listen("0.0.0.0", httpPort);
}

void rpcWorker(std::shared_ptr<discordpp::Client> client) {
    while (running) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            
            if (!eventQueue.empty()) {
                EventData data = eventQueue.front();
                eventQueue.pop();
                
                lastDataTime = std::time(nullptr);
                rpcCleared = false;
                
                if (data.type == "BeatmapInitialized") {
                    long long currentTime = getCurrentTimestamp();
                    int duration = std::stoi(data.metadata["duration"]);
                    long long endTime = currentTime + duration;
                    
                    storedSongData = data;
                    totalPausedDuration = 0;
                    pauseStartTime = 0;
                    
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState(data.metadata["difficulty"] + " | Solo");
                    activity.SetDetails(data.metadata["author"] + " - " + data.metadata["title"] + " | " + joinMappers(data.mappers));
                    
                    // Set timestamps
                    discordpp::ActivityTimestamps timestamps;
                    timestamps.SetStart(currentTime * 1000);  // Convert to milliseconds
                    timestamps.SetEnd(endTime * 1000);
                    activity.SetTimestamps(timestamps);
                    
                    // Set assets with small image
                    discordpp::ActivityAssets assets;
                    assets.SetSmallImage("quest");
                    assets.SetSmallText("Meta Quest");
                    activity.SetAssets(assets);
                    
                    client->UpdateRichPresence(activity, [](auto result) {
                        if (!result.Successful()) {
                            std::cerr << "Failed to update rich presence: " << result.Error() << std::endl;
                        }
                    });
                }
                else if (data.type == "MainMenuInitialized") {
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Status: Main Menu");
                    
                    discordpp::ActivityAssets assets;
                    assets.SetSmallImage("quest");
                    assets.SetSmallText("Meta Quest");
                    activity.SetAssets(assets);
                    
                    client->UpdateRichPresence(activity, [](auto result) {});
                }
                else if (data.type == "LevelSelectionMenuInitialized") {
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Status: Level Selection Menu");
                    
                    discordpp::ActivityAssets assets;
                    assets.SetSmallImage("quest");
                    assets.SetSmallText("Meta Quest");
                    activity.SetAssets(assets);
                    
                    client->UpdateRichPresence(activity, [](auto result) {});
                }
                else if (data.type == "BeatmapCleared") {
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Status: Cleared | " + data.metadata["difficulty"]);
                    activity.SetDetails(data.metadata["author"] + " - " + data.metadata["title"] + " | " + joinMappers(data.mappers));
                    
                    discordpp::ActivityAssets assets;
                    assets.SetSmallImage("quest");
                    assets.SetSmallText("Meta Quest");
                    activity.SetAssets(assets);
                    
                    client->UpdateRichPresence(activity, [](auto result) {});
                }
                else if (data.type == "BeatmapFailed") {
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Status: Failed | " + storedSongData.metadata["difficulty"]);
                    activity.SetDetails(storedSongData.metadata["author"] + " - " + storedSongData.metadata["title"] + " | " + joinMappers(storedSongData.mappers));
                    
                    discordpp::ActivityAssets assets;
                    assets.SetSmallImage("quest");
                    assets.SetSmallText("Meta Quest");
                    activity.SetAssets(assets);
                    
                    client->UpdateRichPresence(activity, [](auto result) {});
                }
                else if (data.type == "BeatmapPaused") {
                    pauseStartTime = std::time(nullptr);
                    
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Level paused");
                    
                    client->UpdateRichPresence(activity, [](auto result) {});
                }
                else if (data.type == "BeatmapResumed") {
                    time_t currentTime = std::time(nullptr);
                    
                    if (pauseStartTime != 0) {
                        totalPausedDuration += (currentTime - pauseStartTime);
                        pauseStartTime = 0;
                    }
                    
                    int adjustedStart = 0; // In real implementation, store start time with song
                    int adjustedEnd = adjustedStart + std::stoi(storedSongData.metadata["duration"]);
                    
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState(storedSongData.metadata["author"] + " - " + storedSongData.metadata["title"]);
                    activity.SetDetails("Mapped by " + joinMappers(storedSongData.mappers) + " | " + storedSongData.metadata["difficulty"]);
                    
                    client->UpdateRichPresence(activity, [](auto result) {});
                }
                else if (data.type == "LobbyPlayerOnDisconnect" || data.type == "LobbyPlayerOnConnect") {
                    std::cout << "LobbyPlayer Called" << std::endl;
                    if (partyId.empty()) {
                        // Generate a simple UUID (simplified)
                        partyId = "party_" + std::to_string(getCurrentTimestamp());
                    }
                    
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetDetails("Status: Multiplayer Lobby");
                    activity.SetState(data.metadata["playerCount"] + " players waiting...");
                    
                    discordpp::ActivityAssets assets;
                    assets.SetSmallImage("quest");
                    assets.SetSmallText("Meta Quest");
                    activity.SetAssets(assets);
                    
                    // Set party info
                    discordpp::ActivityParty party;
                    party.SetId(partyId);
                    party.SetCurrentSize(std::stoi(data.metadata["playerCount"]));
                    party.SetMaxSize(4);

                    discordpp::ActivitySecrets secrets;
                    secrets.SetJoin(data.metadata["lobbyCode"]);

                    activity.SetSecrets(secrets);
                    activity.SetParty(party);
                    
                    client->UpdateRichPresence(activity, [](auto result) {});
                }
                else if (data.type == "MultiplayerBeatmapInitialized") {
                    partyId = "";
                    long long currentTime = getCurrentTimestamp();
                    int duration = std::stoi(data.metadata["duration"]);
                    long long endTime = currentTime + duration;
                    
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Status: Playing | " + data.metadata["difficulty"] + " | Multiplayer");
                    activity.SetDetails(data.metadata["author"] + " - " + data.metadata["title"] + " | " + joinMappers(data.mappers));
                    
                    // Set timestamps
                    discordpp::ActivityTimestamps timestamps;
                    timestamps.SetStart(currentTime * 1000);
                    timestamps.SetEnd(endTime * 1000);
                    activity.SetTimestamps(timestamps);
                    
                    discordpp::ActivityAssets assets;
                    assets.SetSmallImage("quest");
                    assets.SetSmallText("Meta Quest");
                    activity.SetAssets(assets);
                    
                    client->UpdateRichPresence(activity, [](auto result) {});
                }
            }
        }
        
        // Check inactivity timeout
        if (!rpcCleared && (std::time(nullptr) - lastDataTime) > INACTIVITY_TIMEOUT) {
            // Clear activity by updating with empty state
            discordpp::Activity activity;
            activity.SetType(discordpp::ActivityTypes::Playing);
            client->UpdateRichPresence(activity, [](auto result) {});
            rpcCleared = true;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, char* argv[]) {
    // Parse runtime arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            httpPort = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--port <port>]\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
        }
    }

    std::signal(SIGINT, signalHandler);
    std::cout << "🚀 Initializing Beat Saber Bridge API...\n";

    // Create our Discord Client
    auto client = std::make_shared<discordpp::Client>();

    // Set up logging callback
    client->AddLogCallback([](auto message, auto severity) {
        std::cout << "[" << EnumToString(severity) << "] " << message << std::endl;
    }, discordpp::LoggingSeverity::None);

    // Set up status callback to monitor client connection
    client->SetStatusChangedCallback([client](discordpp::Client::Status status, discordpp::Client::Error error, int32_t errorDetail) {
        std::cout << "🔄 Status changed: " << discordpp::Client::StatusToString(status) << std::endl;

        if (status == discordpp::Client::Status::Ready) {
            std::cout << "✅ Discord client is ready!\n";

        } else if (error != discordpp::Client::Error::None) {
            std::cerr << "❌ Connection error: " << discordpp::Client::ErrorToString(error) << " (detail: " << errorDetail << ")" << std::endl;
        }
    });

    // Generate OAuth2 code verifier for authentication
    auto codeVerifier = client->CreateAuthorizationCodeVerifier();

    // Set up authentication arguments
    discordpp::AuthorizationArgs args{};
    args.SetClientId(APPLICATION_ID);
    args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
    args.SetCodeChallenge(codeVerifier.Challenge());

    std::cout << "🔐 Starting authorization process...\n";

    // Begin authentication process
    client->Authorize(args, [client, codeVerifier](auto result, auto code, auto redirectUri) {
        if (!result.Successful()) {
            std::cerr << "❌ Authorization failed: " << result.Error() << std::endl;
            return;
        } else {
            std::cout << "✅ Authorization successful! Getting access token...\n";

            // Exchange auth code for access token
            client->GetToken(APPLICATION_ID, code, codeVerifier.Verifier(), redirectUri,
                [client](discordpp::ClientResult result,
                std::string accessToken,
                std::string refreshToken,
                discordpp::AuthorizationTokenType tokenType,
                int32_t expiresIn,
                std::string scope) {
                if (!result.Successful()) {
                    std::cerr << "❌ GetToken failed: " << result.Error() << std::endl;
                    return;
                }
                std::cout << "🔓 Access token received! Establishing connection...\n";
                // Next Step: Update the token and connect
                client->UpdateToken(discordpp::AuthorizationTokenType::Bearer, accessToken, [client](discordpp::ClientResult result) {
                    if(result.Successful()) {
                        std::cout << "🔑 Token updated, connecting to Discord...\n";
                        client->Connect();
                    }
                });
            });
        }
    });

    // Start RPC worker thread
    std::thread rpcWorkerThread(rpcWorker, client);
    rpcWorkerThread.detach();

    // Start HTTP server thread
    std::thread httpServerThread(httpServer);
    httpServerThread.detach();

    std::cout << "HTTP Server listening on http://0.0.0.0:" << httpPort << "\n";

    // Keep application running to allow SDK to receive events and callbacks
    while (running) {
        discordpp::RunCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}