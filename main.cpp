#include <fstream>
#include <ostream>
#define DISCORDPP_IMPLEMENTATION
#include "include/discordpp.h"
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
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

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <array>
#include <nlohmann/json.hpp>

#include <zip.h>

#include <filesystem>

namespace fs = std::filesystem;

uint64_t applicationId = 1028340906740420711;

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
time_t totalPausedDuration = 0;

int httpPort = 8080;

bool downloadFile(const std::string& host,
                  const std::string& path,
                  const fs::path& outputFile)
{
    httplib::Client client(host);
    client.set_follow_location(true);

    std::ofstream out(outputFile, std::ios::binary);

    if (!out)
    {
        std::cerr << "Failed to open output file\n";
        return false;
    }

    auto res = client.Get(
        path.c_str(),
        [&](const char* data, size_t data_length)
        {
            out.write(data, data_length);
            return true; // continue receiving
        });

    out.close();

    if (!res)
    {
        std::cerr << "Request failed: ";

        switch (res.error())
        {
            case httplib::Error::Connection:
                std::cerr << "Connection error";
                break;

            case httplib::Error::BindIPAddress:
                std::cerr << "Failed to bind IP address";
                break;

            case httplib::Error::Read:
                std::cerr << "Read error";
                break;

            case httplib::Error::Write:
                std::cerr << "Write error";
                break;

            case httplib::Error::ExceedRedirectCount:
                std::cerr << "Too many redirects";
                break;

            case httplib::Error::Canceled:
                std::cerr << "Request canceled";
                break;

            case httplib::Error::SSLConnection:
                std::cerr << "SSL connection failed";
                break;

            case httplib::Error::SSLLoadingCerts:
                std::cerr << "Failed to load SSL certificates";
                break;

            case httplib::Error::SSLServerVerification:
                std::cerr << "SSL server verification failed";
                break;

            case httplib::Error::UnsupportedMultipartBoundaryChars:
                std::cerr << "Unsupported multipart boundary chars";
                break;

            case httplib::Error::Compression:
                std::cerr << "Compression error";
                break;

            case httplib::Error::ConnectionTimeout:
                std::cerr << "Connection timeout";
                break;

            default:
                std::cerr << "Unknown error";
                break;
        }
        std::cerr << '\n';
        return false;
    }

    if (res->status != 200)
    {
        std::cerr << "HTTP status: "
                  << res->status << '\n';

        return false;
    }

    return true;
}

void extractZip(const fs::path& archivePath, const fs::path& outputDir)
{
    int err = 0;

    zip* z = zip_open(archivePath.string().c_str(), ZIP_RDONLY, &err);
    if (!z) {
        return;
    }

    fs::create_directories(outputDir);

    zip_int64_t numEntries = zip_get_num_entries(z, 0);

    for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(numEntries); ++i)
    {
        zip_stat_t st{};
        if (zip_stat_index(z, i, 0, &st) != 0) {
            continue;
        }

        fs::path entryPath = fs::path(st.name);

        // Prevent ZIP Slip attacks
        if (entryPath.is_absolute() || st.name[0] == '/' ||
            entryPath.string().find("..") != std::string::npos)
        {
            continue;
        }

        fs::path outPath = outputDir / entryPath;

        // Directory entry
        if (st.name[strlen(st.name) - 1] == '/')
        {
            fs::create_directories(outPath);
            continue;
        }

        // Create parent directories
        fs::create_directories(outPath.parent_path());

        zip_file* zf = zip_fopen_index(z, i, 0);
        if (!zf) {
            continue;
        }

        std::ofstream out(outPath, std::ios::binary);
        if (!out) {
            zip_fclose(zf);
            continue;
        }

        constexpr size_t BUFFER_SIZE = 8192;
        std::vector<char> buffer(BUFFER_SIZE);

        zip_int64_t bytesRead = 0;

        while ((bytesRead = zip_fread(zf, buffer.data(), buffer.size())) > 0)
        {
            out.write(buffer.data(), bytesRead);
        }

        zip_fclose(zf);
    }

    zip_close(z);
}

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

// Update rich presence, optionally with a small image asset.
// If the asset is not found in the Discord application, logs a warning and
// retries without it so the presence still shows.
void updatePresence(std::shared_ptr<discordpp::Client> client,
                    discordpp::Activity activity,
                    const std::string& smallImage = "",
                    const std::string& smallText = "") {
    if (!smallImage.empty()) {
        discordpp::Activity withAssets = activity;
        discordpp::ActivityAssets assets;
        assets.SetSmallImage(smallImage);
        if (!smallText.empty()) assets.SetSmallText(smallText);
        withAssets.SetAssets(assets);

        // activity (no assets) is captured for the fallback retry
        client->UpdateRichPresence(withAssets, [client, activity, smallImage](auto result) {
            if (!result.Successful()) {
                std::string err = result.Error();
                if (err.find("asset") != std::string::npos) {
                    std::cerr << "⚠️  Asset '" << smallImage << "' not found in your Discord application "
                                 "(upload it under Rich Presence > Art Assets in the Developer Portal). "
                                 "Retrying without assets.\n";
                    client->UpdateRichPresence(activity, [](auto r) {
                        if (!r.Successful())
                            std::cerr << "❌ Failed to update rich presence: " << r.Error() << std::endl;
                    });
                } else {
                    std::cerr << "❌ Failed to update rich presence: " << err << std::endl;
                }
            }
        });
    } else {
        client->UpdateRichPresence(activity, [](auto result) {
            if (!result.Successful())
                std::cerr << "❌ Failed to update rich presence: " << result.Error() << std::endl;
        });
    }
}

void httpServer() {
    httplib::Server svr;

    svr.Get("/version", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(nlohmann::json({{"version", "0.1.5"}}).dump(), "application/json");
    });

    svr.Post("/update", [](const httplib::Request& req, httplib::Response& res) {
        try {
            fs::path temp_path = fs::temp_directory_path();
            fs::path current_path = fs::current_path();

            std::cout << temp_path << std::endl;

            httplib::Client cli("https://api.github.com");
            auto clientResponse = cli.Get("/repos/RainzDev/BeatSaberBridgeAPI.CPP/releases/latest");

            if (clientResponse) {
                nlohmann::json jsonData = nlohmann::json::parse(clientResponse->body);

                std::array assets = jsonData["assets"];

                std::string downloadUrl;

                #ifdef _WIN32
                    downloadUrl = assets[2]["browser_download_url"];
                #elif __linux__
                    downloadUrl = assets[0]["browser_download_url"];
                #elif __APPLE__
                    #include "TargetConditionals.h"
                    #if TARGET_OS_MAC
                        downloadUrl = assets[1]["browser_download_url"];
                    #endif
                #endif

                fs::path mainTempPath = "temp_BeatSaberBridgeAPI";
                fs::path zipName = "download.zip";

                fs::create_directory(temp_path / mainTempPath);

                downloadUrl.erase(0, 18);

                downloadFile("https://github.com", downloadUrl, temp_path / mainTempPath / zipName);
                extractZip(temp_path / mainTempPath / zipName, temp_path / mainTempPath);

                for (const auto & entry : fs::directory_iterator(temp_path / mainTempPath)) {
                    if (entry != zipName) {
                        std::cout << entry << std::endl;
                        try {
                            #ifdef _WIN32
                                fs::rename(current_path / entry.path().filename(), current_path / entry.path().filename().replace_extension(".old"));
                            #else
                                fs::remove(entry.path().filename());
                            #endif
                            fs::remove(entry.path().filename());
                            fs::copy(entry, current_path / entry.path().filename(), fs::copy_options::overwrite_existing);
                        } catch (const fs::filesystem_error& e) {
                            std::cerr << "Filesystem Error: " << e.what() << '\n';
                            std::cerr << "Error Code: " << e.code().message() << '\n';
                        }
                    }
                }

            } else {
                std::cerr << "Failed to download latest release" << std::endl;
            }



        } catch (const std::exception& e) {
            std::cerr << "❌ Error processing request: " << e.what() << std::endl;
            res.set_content(nlohmann::json({{"status", "error"}, {"message", e.what()}}).dump(), "application/json");
            res.status = 400;
        }
    });

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
                try {

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

                    discordpp::ActivityTimestamps timestamps;
                    timestamps.SetStart(currentTime * 1000);  // Convert to milliseconds
                    timestamps.SetEnd(endTime * 1000);
                    activity.SetTimestamps(timestamps);

                    updatePresence(client, activity, "quest", "Meta Quest");
                }
                else if (data.type == "MainMenuInitialized") {
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Status: Main Menu");

                    updatePresence(client, activity, "quest", "Meta Quest");
                }
                else if (data.type == "LevelSelectionMenuInitialized") {
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Status: Level Selection Menu");

                    updatePresence(client, activity, "quest", "Meta Quest");
                }
                else if (data.type == "BeatmapCleared") {
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Status: Cleared | " + data.metadata["difficulty"]);
                    activity.SetDetails(data.metadata["author"] + " - " + data.metadata["title"] + " | " + joinMappers(data.mappers));

                    updatePresence(client, activity, "quest", "Meta Quest");
                }
                else if (data.type == "BeatmapFailed") {
                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Status: Failed | " + storedSongData.metadata["difficulty"]);
                    activity.SetDetails(storedSongData.metadata["author"] + " - " + storedSongData.metadata["title"] + " | " + joinMappers(storedSongData.mappers));

                    updatePresence(client, activity, "quest", "Meta Quest");
                }
                else if (data.type == "BeatmapPaused") {
                    pauseStartTime = std::time(nullptr);

                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetState("Level paused");

                    updatePresence(client, activity);
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

                    updatePresence(client, activity);
                }
                else if (data.type == "LobbyPlayerOnDisconnect" || data.type == "LobbyPlayerOnConnect") {
                    if (partyId.empty()) {
                        // Generate a simple UUID (simplified)
                        partyId = "party_" + std::to_string(getCurrentTimestamp());
                    }

                    discordpp::Activity activity;
                    activity.SetType(discordpp::ActivityTypes::Playing);
                    activity.SetDetails("Status: Multiplayer Lobby");
                    activity.SetState(data.metadata["playerCount"] + " players waiting...");

                    // Set party info
                    discordpp::ActivityParty party;
                    party.SetId(partyId);
                    party.SetCurrentSize(std::stoi(data.metadata["playerCount"]));
                    party.SetMaxSize(std::stoi(data.metadata["maxPlayerCount"]));

                    discordpp::ActivitySecrets secrets;
                    secrets.SetJoin("bsrpc://BeatTogether/" + data.metadata["lobbyCode"]);

                    activity.SetSecrets(secrets);
                    activity.SetParty(party);

                    updatePresence(client, activity, "quest", "Meta Quest");
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

                    discordpp::ActivityTimestamps timestamps;
                    timestamps.SetStart(currentTime * 1000);
                    timestamps.SetEnd(endTime * 1000);
                    activity.SetTimestamps(timestamps);

                    updatePresence(client, activity, "quest", "Meta Quest");
                }
                } catch (const std::exception& e) {
                    std::cerr << "❌ Error processing event '" << data.type << "': " << e.what() << std::endl;
                }
            }
        }

        // Check inactivity timeout
        if (!rpcCleared && (std::time(nullptr) - lastDataTime) > INACTIVITY_TIMEOUT) {
            discordpp::Activity activity;
            activity.SetType(discordpp::ActivityTypes::Playing);
            client->UpdateRichPresence(activity, [](auto result) {});
            rpcCleared = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // Parse runtime arguments
    bool selfTest = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            httpPort = std::stoi(argv[++i]);
        } else if (arg == "--app-id" && i + 1 < argc) {
            applicationId = std::stoull(argv[++i]);
        } else if (arg == "--self-test") {
            selfTest = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--port <port>] [--app-id <id>] [--self-test]\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
        }
    }

    if (selfTest) {
        auto client = std::make_shared<discordpp::Client>();
        discordpp::RunCallbacks();
        std::cout << "Self-test passed: Discord SDK loaded and initialized successfully\n";
        return 0;
    }

    std::signal(SIGINT, signalHandler);
    std::cout << "🚀 Initializing Beat Saber Bridge API...\n";

    // Create our Discord Client
    auto client = std::make_shared<discordpp::Client>();

    // Set up logging callback
    client->AddLogCallback([](auto message, auto severity) {
        std::cout << "[" << EnumToString(severity) << "] " << message << std::endl;
    }, discordpp::LoggingSeverity::Verbose);

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
    args.SetClientId(applicationId);
    args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
    args.SetCodeChallenge(codeVerifier.Challenge());

    std::cout << "🔐 Starting authorization process...\n";

    // Begin authentication process
    client->Authorize(args, [client, codeVerifier](auto result, auto code, auto redirectUri) {
        if (!result.Successful()) {
            std::cerr << "❌ Authorization failed: " << result.Error() << std::endl;
            return;
        }
        std::cout << "✅ Authorization successful! Getting access token...\n";

            // Exchange auth code for access token
            client->GetToken(applicationId, code, codeVerifier.Verifier(), redirectUri,
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
                if (result.Successful()) {
                    std::cout << "🔑 Token updated, connecting to Discord...\n";
                    client->Connect();
                }
            });
        });
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
