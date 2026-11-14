// tiled-dash-client-abr-engine.cc
#include "tiled-dash-client-abr-engine.h"
#include "tiled-dash-client.h"

#include "ns3/log.h"
#include <limits> // Used implicitly by some operations, but good to be aware
#include <algorithm>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TiledDashClientAbrEngine");
NS_OBJECT_ENSURE_REGISTERED (TiledDashClientAbrEngine);

TypeId
TiledDashClientAbrEngine::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TiledDashClientAbrEngine")
    .SetParent<Object> ()
    .SetGroupName("Applications")
    .AddConstructor<TiledDashClientAbrEngine> ()
    .AddAttribute("AbrStrategy", "The ABR strategy to use.",
                   EnumValue(AbrStrategy::Simple), // Default strategy
                   MakeEnumAccessor<AbrStrategy>(&TiledDashClientAbrEngine::m_abrAlgorithm),
                   MakeEnumChecker(AbrStrategy::Simple, "Simple",
                                   AbrStrategy::ViewportAware, "ViewportAware",
                                   AbrStrategy::UniformViewportQualityAdaptQuality,"UniformViewportQualityAdaptQuality",
                                   AbrStrategy::CenterTileFirstAdaptQuality,"CenterTileFirstAdaptQuality",
                                   AbrStrategy::AnotherMethod, "AnotherMethod"))
    ;
  return tid;
}

TiledDashClientAbrEngine::TiledDashClientAbrEngine ()
  : m_abrAlgorithm(AbrStrategy::Simple) // Initialize with a default strategy
{
  NS_LOG_FUNCTION (this);
}

TiledDashClientAbrEngine::~TiledDashClientAbrEngine ()
{
  NS_LOG_FUNCTION (this);
}

void
TiledDashClientAbrEngine::SetQualityRates (std::vector<double> qualityRates)
{
  m_qualityRates = qualityRates;
}

void TiledDashClientAbrEngine::SetAbrAlgorithm(AbrStrategy abrAlgorithm)
{
    m_abrAlgorithm = abrAlgorithm;
}


std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::GetSegmentTileQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds()); // Log deadline value

  if (m_qualityRates.empty()) { // should not happen as it is set in client.cc
      NS_LOG_ERROR("Quality rates not set! Returning no qualities.");
      std::map<uint32_t, std::vector<int>> emptyQualityMap;
      emptyQualityMap[segmentId] = std::vector<int>(fovScores.size(), -1); // All unrequested
      return emptyQualityMap;
  }

  if (m_abrAlgorithm == AbrStrategy::Simple) {
    return SimpleAdaptQuality (netRate, segmentId, fovScores, deadline);
  } 
  else if(m_abrAlgorithm == AbrStrategy::ViewportAware){
    return ViewportAwareAdaptQuality(netRate, segmentId, fovScores, deadline);
  }
  else if(m_abrAlgorithm == AbrStrategy::UniformViewportQualityAdaptQuality){
    return UniformViewportQualityAdaptQuality(netRate, segmentId, fovScores, deadline);
  }
  else if(m_abrAlgorithm == AbrStrategy::CenterTileFirstAdaptQuality){
    return CenterTileFirstAdaptQuality(netRate, segmentId, fovScores, deadline);
  }
  else if(m_abrAlgorithm == AbrStrategy::ViewportOnlyAdaptQuality){
    return ViewportOnlyAdaptQuality(netRate, segmentId, fovScores, deadline);
  }
  else if(m_abrAlgorithm == AbrStrategy::ViewportPlusAdaptQuality){
    return ViewportPlusAdaptQuality(netRate, segmentId, fovScores, deadline);
  }
  else {
    // Consider a less fatal default or warning
    NS_LOG_WARN("Unknown or unsupported ABR algorithm: " << static_cast<int>(m_abrAlgorithm) << ". Defaulting to SimpleAdaptQuality.");
    return SimpleAdaptQuality (netRate, segmentId, fovScores, deadline);
  }
}

std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::SimpleAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  // Initialize with -1 (not requested / undecided)
  std::vector<int> tileQualities (fovScores.size(), -1); //-1 isto

  std::vector<uint32_t> neededTileIndices;
  std::vector<uint32_t> lowerQuality;
  for (uint32_t i = 0; i < fovScores.size (); ++i) {
    if (fovScores[i] >= 0 && fovScores[i]<=3) { // Assuming score >= 0 means the tile is needed.,  && fovScores[i] <= 3
      neededTileIndices.push_back (i);
    }
    else{
      lowerQuality.push_back(i);
    }
    
  }
  if (deadline <= Time(0)) {
      NS_LOG_WARN("Deadline is zero or negative. Cannot determine quality. Requesting lowest for needed tiles.");
      for (uint32_t tileIndex : neededTileIndices) {
          tileQualities[tileIndex] = 0; // Request lowest quality
      }
      qualityMap[segmentId] = tileQualities;
      return qualityMap;
  }

  int bestQuality = -1; // Start with an invalid quality index
  double segmentDurationInSeconds = m_client->GetSegmentDuration().GetSeconds(); 
  NS_LOG_DEBUG("Quality rates size: "<<m_qualityRates.size());
  NS_LOG_DEBUG("TileIndiciesSize: "<<neededTileIndices.size());
  for (uint32_t q_idx = 0; q_idx < m_qualityRates.size(); ++q_idx) {   
    NS_LOG_DEBUG("Quality rates size: "<<m_qualityRates[q_idx]);     
    double tileSizeBits = (m_qualityRates[q_idx] * segmentDurationInSeconds);
    NS_LOG_DEBUG("Needed tile indicies "<<neededTileIndices.size());
    double totalSizeNeededBits = tileSizeBits* neededTileIndices.size();
    NS_LOG_DEBUG("Total SizeNeededBits "<<totalSizeNeededBits);
    // Required network rate to download these needed tiles within the deadline
    NS_LOG_DEBUG("Deadline: "<<deadline.GetSeconds());
    double requiredNetRateBps = (totalSizeNeededBits) / deadline.GetSeconds(); // Convert to bits/sec
    NS_LOG_DEBUG("RequiredNet: "<<requiredNetRateBps<<" NetRate: "<<netRate);
    if (requiredNetRateBps <= netRate) { 
      bestQuality = q_idx; // This quality fits,q_idx
    } else {
      // If this quality (and thus any higher quality) doesn't fit, break.
      break;
    }
  }

  if (bestQuality == -1) {
    NS_LOG_WARN("No quality level fits within the deadline and netRate. Selecting lowest quality (0) for needed tiles.");
    bestQuality = 0; // Default to lowest quality
    if (m_qualityRates.empty()) { // Should have been caught earlier
        NS_LOG_ERROR("Cannot select lowest quality as m_qualityRates is empty.");
        qualityMap[segmentId] = tileQualities; // All -1
        return qualityMap;
    }
  }

  NS_LOG_INFO("Segment " << segmentId << ": Selected quality level " << bestQuality << " for " << neededTileIndices.size() << " tiles.");
  NS_LOG_DEBUG("Segment " << segmentId << ": Selected quality level " << bestQuality << " for " << neededTileIndices.size() << " tiles.");
  for (uint32_t tileIndex : neededTileIndices) {
    tileQualities[tileIndex] = bestQuality;
  }
  //
  for(uint32_t tileIndex: lowerQuality){
    tileQualities[tileIndex]=0;
  }
  //
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}

std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::ViewportAwareAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);

  // ----- Deadline safeguard (same as before) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }

  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // ----- Pre‑compute per‑tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }

  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");

  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality

  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;

  // ----- Process categories in priority order (viewport → adjacent → tier2 → outside → notUsed) -----
  std::vector<std::pair<std::vector<uint32_t>*, std::string>> categories = {
    {&viewportTiles, "viewport"},
    {&adjacentTiles, "adjacent"},
    {&tier2Tiles,   "tier2"},
    {&outsideTiles, "outside"},
    {&notUsed,      "notUsed"}   // lowest priority
  };

  for (auto& cat : categories) {
    std::vector<uint32_t>* tilesVec = cat.first;
    std::string catName = cat.second;
    uint32_t numTiles = tilesVec->size();
    if (numTiles == 0) continue;

    // Find the highest quality q (>=1) such that upgrading ALL tiles in this category
    // from quality 0 to q fits within the remaining budget.
    int bestQ = 0;  // start with no upgrade (keep quality 0)
    for (uint32_t q = 1; q < m_qualityRates.size(); ++q) {
      // Additional cost to upgrade the whole category from 0 to q
      double upgradeCost = numTiles * (perTileSizeBits[q] - perTileSizeBits[0]);
      if (upgradeCost <= remainingBits) {
        bestQ = q;
      } else {
        break;  // higher q would cost even more
      }
    }

    if (bestQ > 0) {
      double usedBits = numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]);
      remainingBits -= usedBits;
      NS_LOG_DEBUG("Category " << catName << ": upgrading " << numTiles << " tiles to quality "
                   << bestQ << ", used extra " << usedBits << " bits, remaining " << remainingBits);
      for (uint32_t idx : *tilesVec) {
        tileQualities[idx] = bestQ;
      }
    } else {
      NS_LOG_DEBUG("Category " << catName << ": no upgrade possible, staying at quality 0");
    }
  }

  NS_LOG_INFO("Segment " << segmentId << ": ViewportAware completed.");
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}

std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::UniformViewportQualityAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);

  // --- Deadline safeguard (exactly as in SimpleAdaptQuality) ---
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using lowest quality for needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Collect needed tiles (FOV score >= 0)
  std::vector<uint32_t> allNeededTiles;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] >= 0) allNeededTiles.push_back(i);
  }

  if (allNeededTiles.empty()) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Line 1: lowest quality for all needed tiles
  for (uint32_t tileIndex : allNeededTiles) {
    tileQualities[tileIndex] = 0;
  }

  // Lines 2-3: Early buffering – use actual client buffer duration
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
  double segmentDurationSecs = m_client->GetSegmentDuration().GetSeconds();
  if (segmentId <= static_cast<uint32_t>(bufferDuration / segmentDurationSecs)) {
    NS_LOG_INFO("Early buffering phase (buffer=" << bufferDuration << "s). Using lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Line 4: Budget = available bandwidth * deadline (safeguard)
  //double budgetBits = netRate * deadline.GetSeconds();
  double budgetBits = netRate * segmentDurationSecs;
  NS_LOG_DEBUG("Budget (netRate * deadline): " << budgetBits << " bits");

  // Pre‑compute per‑tile size for each quality (bits)
  //const double T_TOTAL = 24.0; // total number of tiles (adjust as needed)
  const double T_TOTAL=m_client->GetHorTiles()*m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurationSecs) / T_TOTAL);
  }

  // Lines 6-7: lowest quality total > budget?
  double totalSizeLowest = 0;
  for (uint32_t tileIndex : allNeededTiles) {
    totalSizeLowest += perTileSizeBits[0];
  }
  if (totalSizeLowest > budgetBits) {
    NS_LOG_WARN("Lowest quality total exceeds budget. Using lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Lines 8-9: highest quality total fits in budget?
  uint32_t highestQuality = m_qualityRates.size() - 1;
  double totalSizeHighest = 0;
  for (uint32_t tileIndex : allNeededTiles) {
    totalSizeHighest += perTileSizeBits[highestQuality];
  }
  if (totalSizeHighest <= budgetBits) {
    NS_LOG_INFO("Highest quality fits for all tiles.");
    for (uint32_t tileIndex : allNeededTiles) {
      tileQualities[tileIndex] = highestQuality;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Line 10: distances (using fovScores as proxy – lower = closer)
  std::vector<std::pair<uint32_t, int>> tilesWithDistance;
  for (uint32_t idx : allNeededTiles) {
    tilesWithDistance.push_back({idx, fovScores[idx]});
  }

  // Lines 11-13: split viewport (distance <= vp/2) and outside
  std::vector<std::pair<uint32_t, int>> tilesInViewport, tilesOutside;
  for (const auto& p : tilesWithDistance) {
    if (p.second == 0)  // fovScore 0 -> inside viewport
      tilesInViewport.push_back(p);
    else
      tilesOutside.push_back(p);
  }

  auto sortByDistance = [](const auto& a, const auto& b) { return a.second < b.second; };
  std::sort(tilesInViewport.begin(), tilesInViewport.end(), sortByDistance);
  std::sort(tilesOutside.begin(), tilesOutside.end(), sortByDistance);

  // Current bits used (all at lowest quality)
  double currentBitsUsed = totalSizeLowest;

  // Lines 14-21: process viewport first, then outside
  std::vector<std::vector<std::pair<uint32_t, int>>*> groups = {&tilesInViewport, &tilesOutside};

  for (auto* group : groups) {
    // Incremental quality levels (q = 1 .. highestQuality)
    for (uint32_t q = 1; q <= highestQuality; ++q) {
      for (auto& tilePair : *group) {
        uint32_t tileIdx = tilePair.first;
        if (tileQualities[tileIdx] >= (int)q) continue;

        // Cost to upgrade from (q-1) to q (per‑tile)
        double cost = perTileSizeBits[q] - perTileSizeBits[q-1];

        if (currentBitsUsed + cost > budgetBits) {
          NS_LOG_DEBUG("Budget exceeded. Returning qualities.");
          qualityMap[segmentId] = tileQualities;
          return qualityMap;
        }

        currentBitsUsed += cost;
        tileQualities[tileIdx] = q;
        NS_LOG_DEBUG("Tile " << tileIdx << " upgraded to Q" << q
                     << ", bits used: " << currentBitsUsed);
      }
    }
  }

  NS_LOG_INFO("Segment " << segmentId << ": UVQ done. Bits used: " << currentBitsUsed);
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::CenterTileFirstAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);

  // --- Deadline safeguard ---
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using lowest quality for needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Collect needed tiles
  std::vector<uint32_t> allNeededTiles;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] >= 0) allNeededTiles.push_back(i);
  }

  if (allNeededTiles.empty()) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Line 1: lowest quality for all
  for (uint32_t tileIndex : allNeededTiles) {
    tileQualities[tileIndex] = 0;
  }

  // Lines 2-3: early buffering (actual buffer duration)
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
  double segmentDurationSecs = m_client->GetSegmentDuration().GetSeconds();
  if (segmentId <= static_cast<uint32_t>(bufferDuration / segmentDurationSecs)) {
    NS_LOG_INFO("Early buffering phase. Using lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Line 4: budget = netRate * deadline (safeguard)
  //double budgetBits = netRate * deadline.GetSeconds();
  double budgetBits = netRate * segmentDurationSecs;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");

  // Per‑tile sizes
  //const double T_TOTAL = 24.0;
  const double T_TOTAL=m_client->GetHorTiles()*m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurationSecs) / T_TOTAL);
  }

  // Distances (using fovScores)
  std::vector<std::pair<uint32_t, int>> tilesWithDistances;
  for (uint32_t idx : allNeededTiles) {
    tilesWithDistances.push_back({idx, fovScores[idx]});
  }

  // Current bits at lowest quality
  double currentBitsUsed = 0;
  for (uint32_t idx : allNeededTiles) {
    currentBitsUsed += perTileSizeBits[0];
  }

  // Lines 8-9: lowest quality exceeds budget?
  if (currentBitsUsed > budgetBits) {
    NS_LOG_WARN("Lowest quality exceeds budget. Using lowest.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Lines 10-11: highest quality fits?
  uint32_t highestQuality = m_qualityRates.size() - 1;
  double totalSizeHighest = 0;
  for (uint32_t idx : allNeededTiles) {
    totalSizeHighest += perTileSizeBits[highestQuality];
  }
  if (totalSizeHighest <= budgetBits) {
    NS_LOG_INFO("Highest quality fits for all.");
    for (uint32_t idx : allNeededTiles) {
      tileQualities[idx] = highestQuality;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Line 12: sort tiles by distance (ascending – closest first)
  std::sort(tilesWithDistances.begin(), tilesWithDistances.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

  // CTF greedy loop (as per Algorithm 2: incremental upgrades, stop on budget exhaustion)
  for (auto& tilePair : tilesWithDistances) {
    uint32_t tileIdx = tilePair.first;
    for (uint32_t q = 1; q <= highestQuality; ++q) {
      // Incremental cost from (q-1) to q
      double cost = perTileSizeBits[q] - perTileSizeBits[q-1];
      if (currentBitsUsed + cost > budgetBits) {
        // Budget exhausted – return immediately (no further tiles processed)
        NS_LOG_DEBUG("Budget exhausted at tile " << tileIdx << ", quality " << q);
        qualityMap[segmentId] = tileQualities;
        return qualityMap;
      }
      currentBitsUsed += cost;
      tileQualities[tileIdx] = q;
      NS_LOG_DEBUG("CTF: tile " << tileIdx << " -> Q" << q
                   << " (distance " << tilePair.second << "), bits used: " << currentBitsUsed);
    }
  }

  NS_LOG_INFO("Segment " << segmentId << ": CTF completed. Bits used: " << currentBitsUsed);
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}

std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::ViewportOnlyAdaptQuality (
    double netRate, uint32_t segmentId,
    const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION(this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;

  // ----- Count viewport tiles (fovScore == 0) -----
  std::vector<uint32_t> viewportTiles;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0) {
      viewportTiles.push_back(i);
    }
  }

  uint32_t numViewportTiles = viewportTiles.size();

  // ----- Dynamically update minTiles for the whole client -----
  // We still download the background at Q0, so a segment will have
  // many more tiles than just the viewport. Setting minTiles to the
  // viewport count (or a small margin) lets the buffer become ready
  // as soon as the important tiles are present.
  if (m_client && m_client->GetMediaBuffer()) {
    // Optional safety margin (e.g. +2 or +10% of viewport)
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);  // or numViewportTiles + 2;, change based on the number of minTiles you want to see
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("ViewportOnly: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }

  // ----- Prepare per-tile sizes and budget -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  double budgetBits = netRate * segmentDurSec;
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();

  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }

  // ----- All tiles start at quality 0 (lowest) -----
  std::vector<int> tileQualities(fovScores.size(), 0);

  // No viewport tiles → keep everything at Q0
  if (viewportTiles.empty()) {
    NS_LOG_INFO("No viewport tiles. All tiles at quality 0.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  if (deadline <= Time(0)) {
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // ----- Base cost = all tiles at Q0 -----
  double baseCost = fovScores.size() * perTileSizeBits[0];
  if (baseCost > budgetBits) {
    NS_LOG_WARN("Budget insufficient for Q0 on all tiles. Keeping all at 0.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  double remaining = budgetBits - baseCost;

  // ----- Upgrade only the viewport tiles as high as the remaining budget allows -----
  int bestQ = 0;
  for (uint32_t q = 1; q < m_qualityRates.size(); ++q) {
    double upgradeCost = viewportTiles.size() * (perTileSizeBits[q] - perTileSizeBits[0]);
    if (upgradeCost <= remaining) {
      bestQ = q;
    } else {
      break;
    }
  }

  for (uint32_t idx : viewportTiles) {
    tileQualities[idx] = bestQ;
  }
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}

std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::ViewportPlusAdaptQuality (
    double netRate, uint32_t segmentId,
    const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION(this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities(fovScores.size(), -1);

  if (deadline <= Time(0)) {
    // fallback: quality 0 for all tiles
    for (uint32_t i = 0; i < fovScores.size(); ++i)
      tileQualities[i] = 0;
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  // Identify viewport tiles
  std::vector<uint32_t> viewportTiles;
  for (uint32_t i = 0; i < fovScores.size(); ++i)
    if (fovScores[i] == 0) viewportTiles.push_back(i);

  // If no viewport tiles, download all at quality 0 (like VP_Plus would still download all)
  if (viewportTiles.empty()) {
    for (uint32_t i = 0; i < fovScores.size(); ++i)
      tileQualities[i] = 0;
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }

  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  double budgetBits = netRate * segmentDurSec;
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();

  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates)
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);

  // Base cost: all non-viewport tiles at quality 0
  double baseCost = (fovScores.size() - viewportTiles.size()) * perTileSizeBits[0];
  double remaining = budgetBits - baseCost;

  // Find highest quality for viewport tiles that fits in remaining
  int bestQ = 0;
  for (uint32_t q = 0; q < m_qualityRates.size(); ++q) {
    double upgradeCost = viewportTiles.size() * perTileSizeBits[q];
    if (upgradeCost <= remaining) bestQ = q;
    else break;
  }

  // Assign qualities
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    tileQualities[i] = 0; // all tiles get at least Q0
  }
  for (uint32_t idx : viewportTiles) {
    tileQualities[idx] = bestQ;
  }

  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//New additions 05.10
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::Bola360AdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("Bola360AdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- FoV score -> view probability of one tile -----
  // (VSE360 algorithms work with a view probability per tile; here it comes only from the FoV score)
  const double wViewport = 1.0, wAdjacent = 0.5, wTier2 = 0.25, wOutside = 0.1, wNotUsed = 0.0;
  double wSum = viewportTiles.size() * wViewport + adjacentTiles.size() * wAdjacent
              + tier2Tiles.size() * wTier2 + outsideTiles.size() * wOutside + notUsed.size() * wNotUsed;
  double pViewport = (wSum > 0) ? wViewport / wSum : 1.0 / totalTiles;
  double pAdjacent = (wSum > 0) ? wAdjacent / wSum : 1.0 / totalTiles;
  double pTier2    = (wSum > 0) ? wTier2    / wSum : 1.0 / totalTiles;
  double pOutside  = (wSum > 0) ? wOutside  / wSum : 1.0 / totalTiles;
  double pNotUsed  = (wSum > 0) ? wNotUsed  / wSum : 1.0 / totalTiles;
 
  // ----- Buffer state -----
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
  const double maxBufferSec = 5.0; // max buffered duration (same value as in ApplyBufferAwareAdjustments)
 
  // ----- BOLA360 parameters (values used in VSE360) -----
  const double gamma = 0.2;
  const double vCoeff = 0.95;
  const double beta = 0.0;
  const double vAlpha = 2.0;
 
  // Utility of each quality: v_q = ln(vAlpha * size_q / size_0)
  std::vector<double> values;
  for (uint32_t q = 0; q < perTileSizeBits.size(); ++q) {
    values.push_back(std::log(vAlpha * perTileSizeBits[q] / perTileSizeBits[0]));
  }
 
  double D = totalTiles;                                         // tiles per segment
  double bufferCapacityTiles = maxBufferSec / segmentDurSec * D;  // buffer capacity in tiles
  double bufferTiles = bufferDuration / segmentDurSec * D;        // current buffer in tiles
  double V = vCoeff * (bufferCapacityTiles - D) / (values.back() + gamma * segmentDurSec - beta);
  if (V <= 0) {
    NS_LOG_WARN("BOLA360: V <= 0 (buffer capacity too small). Using small V.");
    V = 1e-3;
  }
  NS_LOG_DEBUG("BOLA360: V=" << V << " bufferTiles=" << bufferTiles);
 
  // ----- Process categories in priority order (viewport -> adjacent -> tier2 -> outside -> notUsed) -----
  std::vector<std::pair<std::vector<uint32_t>*, std::string>> categories = {
    {&viewportTiles, "viewport"},
    {&adjacentTiles, "adjacent"},
    {&tier2Tiles,   "tier2"},
    {&outsideTiles, "outside"},
    {&notUsed,      "notUsed"}   // lowest priority
  };
  std::vector<double> categoryProb = {pViewport, pAdjacent, pTier2, pOutside, pNotUsed};
 
  for (uint32_t c = 0; c < categories.size(); ++c) {
    std::vector<uint32_t>* tilesVec = categories[c].first;
    std::string catName = categories[c].second;
    uint32_t numTiles = tilesVec->size();
    if (numTiles == 0) continue;
    double p = categoryProb[c];
 
    // BOLA360 decision: quality with maximum rho
    int bestQ = 0;
    double bestRho = -std::numeric_limits<double>::infinity();
    bool anyPositive = false;
    for (uint32_t q = 0; q < m_qualityRates.size(); ++q) {
      double numerator = V * (p * values[q] + gamma * segmentDurSec - beta) - bufferTiles * p;
      double rho = numerator / perTileSizeBits[q];
      if (numerator > 0) anyPositive = true;
      if (rho > bestRho) {
        bestRho = rho;
        bestQ = q;
      }
    }
    if (!anyPositive) {
      bestQ = (p > 0) ? (int)m_qualityRates.size() - 1 : 0;
    }
 
    // Lower the BOLA quality until the upgrade of the whole category fits
    while (bestQ > 0 && numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]) > remainingBits) {
      bestQ--;
    }
 
    if (bestQ > 0) {
      double usedBits = numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]);
      remainingBits -= usedBits;
      NS_LOG_DEBUG("Category " << catName << ": BOLA360 quality " << bestQ << ", used extra "
                   << usedBits << " bits, remaining " << remainingBits);
      for (uint32_t idx : *tilesVec) {
        tileQualities[idx] = bestQ;
      }
    } else {
      NS_LOG_DEBUG("Category " << catName << ": staying at quality 0");
    }
  }
 
  NS_LOG_INFO("Segment " << segmentId << ": BOLA360 completed. Buffer " << bufferDuration << "s");
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//Flare
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::FlareAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("FlareAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- FoV score -> view probability of one tile -----
  // (VSE360 algorithms work with a view probability per tile; here it comes only from the FoV score)
  const double wViewport = 1.0, wAdjacent = 0.5, wTier2 = 0.25, wOutside = 0.1, wNotUsed = 0.0;
  double wSum = viewportTiles.size() * wViewport + adjacentTiles.size() * wAdjacent
              + tier2Tiles.size() * wTier2 + outsideTiles.size() * wOutside + notUsed.size() * wNotUsed;
  double pViewport = (wSum > 0) ? wViewport / wSum : 1.0 / totalTiles;
  double pAdjacent = (wSum > 0) ? wAdjacent / wSum : 1.0 / totalTiles;
  double pTier2    = (wSum > 0) ? wTier2    / wSum : 1.0 / totalTiles;
  double pOutside  = (wSum > 0) ? wOutside  / wSum : 1.0 / totalTiles;
  double pNotUsed  = (wSum > 0) ? wNotUsed  / wSum : 1.0 / totalTiles;
 
  // ----- Buffer state -----
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
  const double maxBufferSec = 10.0; // max buffered duration (same value as in ApplyBufferAwareAdjustments)
 
  // ----- Flare budget -----
  double bufferOccupancy = std::min(1.0, bufferDuration / maxBufferSec);
  double xi = 0.3 + 0.3 * bufferOccupancy;
  double flareBudgetBits = bufferDuration * netRate * xi;
  double flareRemainingBits = std::min(remainingBits, flareBudgetBits - totalLowestBits);
  NS_LOG_DEBUG("Flare: xi=" << xi << " flareBudget=" << flareBudgetBits << " remaining=" << flareRemainingBits);
  if (flareRemainingBits <= 0) {
    NS_LOG_INFO("Flare: budget below lowest quality. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Search: one quality per category, viewport >= adjacent >= tier2 >= outside -----
  int highestQuality = m_qualityRates.size() - 1;
  int bestV = 0, bestA = 0, bestT = 0, bestO = 0;
  double bestUtility = -1;
  for (int qV = highestQuality; qV >= 0; --qV) {
    for (int qA = qV; qA >= 0; --qA) {
      for (int qT = qA; qT >= 0; --qT) {
        for (int qO = qT; qO >= 0; --qO) {
          double extraBits = viewportTiles.size() * (perTileSizeBits[qV] - perTileSizeBits[0])
                           + adjacentTiles.size() * (perTileSizeBits[qA] - perTileSizeBits[0])
                           + tier2Tiles.size()    * (perTileSizeBits[qT] - perTileSizeBits[0])
                           + outsideTiles.size()  * (perTileSizeBits[qO] - perTileSizeBits[0]);
          if (extraBits > flareRemainingBits) continue;
 
          // expected viewed bitrate
          double utility = (viewportTiles.size() * pViewport * perTileSizeBits[qV]
                          + adjacentTiles.size() * pAdjacent * perTileSizeBits[qA]
                          + tier2Tiles.size()    * pTier2    * perTileSizeBits[qT]
                          + outsideTiles.size()  * pOutside  * perTileSizeBits[qO]
                          + notUsed.size()       * pNotUsed  * perTileSizeBits[0]) / segmentDurSec;
          if (utility > bestUtility) {
            bestUtility = utility;
            bestV = qV; bestA = qA; bestT = qT; bestO = qO;
          }
        }
      }
    }
  }
 
  for (uint32_t idx : viewportTiles) tileQualities[idx] = bestV;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = bestA;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = bestT;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = bestO;
 
  NS_LOG_INFO("Segment " << segmentId << ": Flare completed. Q viewport/adjacent/tier2/outside = "
              << bestV << "/" << bestA << "/" << bestT << "/" << bestO);
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//Pro Dash
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::ProbDashAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("ProbDashAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- FoV score -> view probability of one tile -----
  // (VSE360 algorithms work with a view probability per tile; here it comes only from the FoV score)
  const double wViewport = 1.0, wAdjacent = 0.5, wTier2 = 0.25, wOutside = 0.1, wNotUsed = 0.0;
  double wSum = viewportTiles.size() * wViewport + adjacentTiles.size() * wAdjacent
              + tier2Tiles.size() * wTier2 + outsideTiles.size() * wOutside + notUsed.size() * wNotUsed;
  double pViewport = (wSum > 0) ? wViewport / wSum : 1.0 / totalTiles;
  double pAdjacent = (wSum > 0) ? wAdjacent / wSum : 1.0 / totalTiles;
  double pTier2    = (wSum > 0) ? wTier2    / wSum : 1.0 / totalTiles;
  double pOutside  = (wSum > 0) ? wOutside  / wSum : 1.0 / totalTiles;
  double pNotUsed  = (wSum > 0) ? wNotUsed  / wSum : 1.0 / totalTiles;
 
  // ----- Buffer state -----
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
  const double maxBufferSec = 10.0; // max buffered duration (same value as in ApplyBufferAwareAdjustments)
 
  // ----- 360ProbDash parameters (values used in VSE360) -----
  const double targetBuffer = 0.7 * maxBufferSec;
  const double eta = 0.2;
  const double vAlpha = 2.0;
 
  // ----- Target-buffer rate control -----
  double targetRate = netRate * (bufferDuration - targetBuffer + segmentDurSec) / segmentDurSec;
  double probDashBudgetBits = std::max(targetRate * segmentDurSec, totalLowestBits);
  double probDashRemainingBits = std::min(remainingBits, probDashBudgetBits - totalLowestBits);
  NS_LOG_DEBUG("ProbDash: targetRate=" << targetRate << " remaining=" << probDashRemainingBits);
 
  // Distortion of each quality: d_q = -ln(vAlpha * size_q / size_0)
  std::vector<double> distortion;
  for (uint32_t q = 0; q < perTileSizeBits.size(); ++q) {
    distortion.push_back(-std::log(vAlpha * perTileSizeBits[q] / perTileSizeBits[0]));
  }
  double D = totalTiles;
  double s = 4.0 * M_PI / D;
 
  // ----- Search: one quality per category, viewport >= adjacent >= tier2 >= outside -----
  int highestQuality = m_qualityRates.size() - 1;
  int bestV = 0, bestA = 0, bestT = 0, bestO = 0;
  double bestObjective = std::numeric_limits<double>::infinity();
  for (int qV = highestQuality; qV >= 0; --qV) {
    for (int qA = qV; qA >= 0; --qA) {
      for (int qT = qA; qT >= 0; --qT) {
        for (int qO = qT; qO >= 0; --qO) {
          double extraBits = viewportTiles.size() * (perTileSizeBits[qV] - perTileSizeBits[0])
                           + adjacentTiles.size() * (perTileSizeBits[qA] - perTileSizeBits[0])
                           + tier2Tiles.size()    * (perTileSizeBits[qT] - perTileSizeBits[0])
                           + outsideTiles.size()  * (perTileSizeBits[qO] - perTileSizeBits[0]);
          if (extraBits > probDashRemainingBits) continue;
 
          // Phi: expected distortion
          double phi = (viewportTiles.size() * pViewport * distortion[qV]
                      + adjacentTiles.size() * pAdjacent * distortion[qA]
                      + tier2Tiles.size()    * pTier2    * distortion[qT]
                      + outsideTiles.size()  * pOutside  * distortion[qO]
                      + notUsed.size()       * pNotUsed  * distortion[0]) / D;
          // Psi: spatial quality variance
          double psi = (viewportTiles.size() * pViewport * s * std::pow(distortion[qV] - phi, 2)
                      + adjacentTiles.size() * pAdjacent * s * std::pow(distortion[qA] - phi, 2)
                      + tier2Tiles.size()    * pTier2    * s * std::pow(distortion[qT] - phi, 2)
                      + outsideTiles.size()  * pOutside  * s * std::pow(distortion[qO] - phi, 2)
                      + notUsed.size()       * pNotUsed  * s * std::pow(distortion[0] - phi, 2)) / D;
          double objective = phi + eta * psi;
          if (objective < bestObjective) {
            bestObjective = objective;
            bestV = qV; bestA = qA; bestT = qT; bestO = qO;
          }
        }
      }
    }
  }
 
  for (uint32_t idx : viewportTiles) tileQualities[idx] = bestV;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = bestA;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = bestT;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = bestO;
 
  NS_LOG_INFO("Segment " << segmentId << ": 360ProbDash completed. Q viewport/adjacent/tier2/outside = "
              << bestV << "/" << bestA << "/" << bestT << "/" << bestO);
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//Pano adapt 05.10
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::PanoAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("PanoAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- FoV score -> view probability of one tile -----
  // (VSE360 algorithms work with a view probability per tile; here it comes only from the FoV score)
  const double wViewport = 1.0, wAdjacent = 0.5, wTier2 = 0.25, wOutside = 0.1, wNotUsed = 0.0;
  double wSum = viewportTiles.size() * wViewport + adjacentTiles.size() * wAdjacent
              + tier2Tiles.size() * wTier2 + outsideTiles.size() * wOutside + notUsed.size() * wNotUsed;
  double pViewport = (wSum > 0) ? wViewport / wSum : 1.0 / totalTiles;
  double pAdjacent = (wSum > 0) ? wAdjacent / wSum : 1.0 / totalTiles;
  double pTier2    = (wSum > 0) ? wTier2    / wSum : 1.0 / totalTiles;
  double pOutside  = (wSum > 0) ? wOutside  / wSum : 1.0 / totalTiles;
  double pNotUsed  = (wSum > 0) ? wNotUsed  / wSum : 1.0 / totalTiles;
 
  // ----- Per-tile view probability and priority order -----
  std::vector<double> tileProb(fovScores.size(), 0.0);
  std::vector<uint32_t> tilesInPriority;
  for (uint32_t idx : viewportTiles) { tileProb[idx] = pViewport; tilesInPriority.push_back(idx); }
  for (uint32_t idx : adjacentTiles) { tileProb[idx] = pAdjacent; tilesInPriority.push_back(idx); }
  for (uint32_t idx : tier2Tiles)   { tileProb[idx] = pTier2;    tilesInPriority.push_back(idx); }
  for (uint32_t idx : outsideTiles) { tileProb[idx] = pOutside;  tilesInPriority.push_back(idx); }
  for (uint32_t idx : notUsed)      { tileProb[idx] = pNotUsed;  tilesInPriority.push_back(idx); }
 
  // Distortion of each quality: (r_max - r_q)^2, rates in Mbps
  int highestQuality = m_qualityRates.size() - 1;
  double rMax = perTileSizeBits[highestQuality] / segmentDurSec / 1e6;
  std::vector<double> distortion;
  for (uint32_t q = 0; q < perTileSizeBits.size(); ++q) {
    double r = perTileSizeBits[q] / segmentDurSec / 1e6;
    distortion.push_back((rMax - r) * (rMax - r));
  }
 
  // ----- Greedy upgrades -----
  while (true) {
    int bestTile = -1;
    double bestGain = 0;
    for (uint32_t idx : tilesInPriority) {
      int q = tileQualities[idx];
      if (q >= highestQuality) continue;
      double cost = perTileSizeBits[q + 1] - perTileSizeBits[q];
      if (cost > remainingBits) continue;
      double gain = tileProb[idx] * (distortion[q] - distortion[q + 1]) / cost;
      if (gain > bestGain) {
        bestGain = gain;
        bestTile = idx;
      }
    }
    if (bestTile == -1) break; // nothing fits / nothing improves
 
    int q = tileQualities[bestTile];
    remainingBits -= perTileSizeBits[q + 1] - perTileSizeBits[q];
    tileQualities[bestTile] = q + 1;
    NS_LOG_DEBUG("Pano: tile " << bestTile << " -> Q" << q + 1 << ", remaining " << remainingBits);
  }
 
  NS_LOG_INFO("Segment " << segmentId << ": Pano completed. Remaining bits: " << remainingBits);
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//Salient VR
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::SalientVRAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("SalientVRAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- FoV score -> view probability of one tile -----
  // (VSE360 algorithms work with a view probability per tile; here it comes only from the FoV score)
  const double wViewport = 1.0, wAdjacent = 0.5, wTier2 = 0.25, wOutside = 0.1, wNotUsed = 0.0;
  double wSum = viewportTiles.size() * wViewport + adjacentTiles.size() * wAdjacent
              + tier2Tiles.size() * wTier2 + outsideTiles.size() * wOutside + notUsed.size() * wNotUsed;
  double pViewport = (wSum > 0) ? wViewport / wSum : 1.0 / totalTiles;
  double pAdjacent = (wSum > 0) ? wAdjacent / wSum : 1.0 / totalTiles;
  double pTier2    = (wSum > 0) ? wTier2    / wSum : 1.0 / totalTiles;
  double pOutside  = (wSum > 0) ? wOutside  / wSum : 1.0 / totalTiles;
  double pNotUsed  = (wSum > 0) ? wNotUsed  / wSum : 1.0 / totalTiles;
 
  // ----- Buffer state -----
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
 
  // ----- SalientVR parameter (value used in VSE360) -----
  const double bufferThreshold = 2.0; // segments kept as safety reserve
 
  double spendableBits = std::min(remainingBits + totalLowestBits, (bufferDuration - bufferThreshold * segmentDurSec) * netRate);
  NS_LOG_DEBUG("SalientVR: buffer=" << bufferDuration << "s spendable=" << spendableBits);
  if (spendableBits <= 0) {
    NS_LOG_INFO("SalientVR: buffer below threshold. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Process categories in priority order (viewport -> adjacent -> tier2 -> outside -> notUsed) -----
  std::vector<std::pair<std::vector<uint32_t>*, std::string>> categories = {
    {&viewportTiles, "viewport"},
    {&adjacentTiles, "adjacent"},
    {&tier2Tiles,   "tier2"},
    {&outsideTiles, "outside"},
    {&notUsed,      "notUsed"}   // lowest priority
  };
  std::vector<double> categoryProb = {pViewport, pAdjacent, pTier2, pOutside, pNotUsed};
 
  for (uint32_t c = 0; c < categories.size(); ++c) {
    std::vector<uint32_t>* tilesVec = categories[c].first;
    std::string catName = categories[c].second;
    if (tilesVec->empty()) continue;
 
    // share of one tile, highest quality that fits in it
    double tileShareBits = spendableBits * categoryProb[c];
    int bestQ = 0;
    for (uint32_t q = 1; q < m_qualityRates.size(); ++q) {
      if (perTileSizeBits[q] <= tileShareBits) {
        bestQ = q;
      } else {
        break;
      }
    }
    NS_LOG_DEBUG("Category " << catName << ": share " << tileShareBits << " bits -> quality " << bestQ);
    for (uint32_t idx : *tilesVec) {
      tileQualities[idx] = bestQ;
    }
  }
 
  NS_LOG_INFO("Segment " << segmentId << ": SalientVR completed.");
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//PRob ViewportAware
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::ProbViewportAwareAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("ProbViewportAwareAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- FoV score -> view probability of one tile -----
  // (VSE360 algorithms work with a view probability per tile; here it comes only from the FoV score)
  const double wViewport = 1.0, wAdjacent = 0.5, wTier2 = 0.25, wOutside = 0.1, wNotUsed = 0.0;
  double wSum = viewportTiles.size() * wViewport + adjacentTiles.size() * wAdjacent
              + tier2Tiles.size() * wTier2 + outsideTiles.size() * wOutside + notUsed.size() * wNotUsed;
  double pViewport = (wSum > 0) ? wViewport / wSum : 1.0 / totalTiles;
  double pAdjacent = (wSum > 0) ? wAdjacent / wSum : 1.0 / totalTiles;
  double pTier2    = (wSum > 0) ? wTier2    / wSum : 1.0 / totalTiles;
  double pOutside  = (wSum > 0) ? wOutside  / wSum : 1.0 / totalTiles;
  double pNotUsed  = (wSum > 0) ? wNotUsed  / wSum : 1.0 / totalTiles;
 
  // ----- Process categories in priority order (viewport -> adjacent -> tier2 -> outside -> notUsed) -----
  std::vector<std::pair<std::vector<uint32_t>*, std::string>> categories = {
    {&viewportTiles, "viewport"},
    {&adjacentTiles, "adjacent"},
    {&tier2Tiles,   "tier2"},
    {&outsideTiles, "outside"},
    {&notUsed,      "notUsed"}   // lowest priority
  };
  std::vector<double> categoryProb = {pViewport, pAdjacent, pTier2, pOutside, pNotUsed};
 
  for (uint32_t c = 0; c < categories.size(); ++c) {
    std::vector<uint32_t>* tilesVec = categories[c].first;
    std::string catName = categories[c].second;
    uint32_t numTiles = tilesVec->size();
    if (numTiles == 0) continue;
 
    // quality closest to netRate * p * segDur
    double targetSizeBits = netRate * categoryProb[c] * segmentDurSec;
    int bestQ = 0;
    for (uint32_t q = 1; q < m_qualityRates.size(); ++q) {
      if (std::fabs(perTileSizeBits[q] - targetSizeBits) < std::fabs(perTileSizeBits[bestQ] - targetSizeBits)) {
        bestQ = q;
      }
    }
 
    // lower until the upgrade of the whole category fits
    while (bestQ > 0 && numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]) > remainingBits) {
      bestQ--;
    }
 
    if (bestQ > 0) {
      double usedBits = numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]);
      remainingBits -= usedBits;
      NS_LOG_DEBUG("Category " << catName << ": target " << targetSizeBits << " bits -> quality " << bestQ
                   << ", remaining " << remainingBits);
      for (uint32_t idx : *tilesVec) {
        tileQualities[idx] = bestQ;
      }
    } else {
      NS_LOG_DEBUG("Category " << catName << ": staying at quality 0");
    }
  }
 
  NS_LOG_INFO("Segment " << segmentId << ": ProbViewportAware completed.");
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//Naive
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::NaiveAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("NaiveAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- K: number of viewport tiles, at least minTiles -----
  uint32_t K = numViewportTiles;
  if (m_client && m_client->GetMediaBuffer()) {
    K = std::max(K, (uint32_t)m_client->GetMediaBuffer()->GetMinTiles());
  }
  K = std::max(K, (uint32_t)1);
  K = std::min(K, totalTiles);
 
  // ----- K most important tiles in FoV priority order -----
  std::vector<uint32_t> tilesInPriority;
  for (uint32_t idx : viewportTiles) tilesInPriority.push_back(idx);
  for (uint32_t idx : adjacentTiles) tilesInPriority.push_back(idx);
  for (uint32_t idx : tier2Tiles)   tilesInPriority.push_back(idx);
  for (uint32_t idx : outsideTiles) tilesInPriority.push_back(idx);
  for (uint32_t idx : notUsed)      tilesInPriority.push_back(idx);
 
  // ----- Equal split of the remaining budget among K tiles -----
  double extraPerTileBits = remainingBits / K;
  int bestQ = 0;
  for (uint32_t q = 1; q < m_qualityRates.size(); ++q) {
    if (perTileSizeBits[q] - perTileSizeBits[0] <= extraPerTileBits) {
      bestQ = q;
    } else {
      break;
    }
  }
 
  for (uint32_t i = 0; i < K; ++i) {
    tileQualities[tilesInPriority[i]] = bestQ;
  }
 
  NS_LOG_INFO("Segment " << segmentId << ": Naive completed. K=" << K << " tiles at quality " << bestQ);
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//ABR mosaic
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::MosaicAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("MosaicAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- FoV score -> view probability of one tile -----
  // (VSE360 algorithms work with a view probability per tile; here it comes only from the FoV score)
  const double wViewport = 1.0, wAdjacent = 0.5, wTier2 = 0.25, wOutside = 0.1, wNotUsed = 0.0;
  double wSum = viewportTiles.size() * wViewport + adjacentTiles.size() * wAdjacent
              + tier2Tiles.size() * wTier2 + outsideTiles.size() * wOutside + notUsed.size() * wNotUsed;
  double pViewport = (wSum > 0) ? wViewport / wSum : 1.0 / totalTiles;
  double pAdjacent = (wSum > 0) ? wAdjacent / wSum : 1.0 / totalTiles;
  double pTier2    = (wSum > 0) ? wTier2    / wSum : 1.0 / totalTiles;
  double pOutside  = (wSum > 0) ? wOutside  / wSum : 1.0 / totalTiles;
  double pNotUsed  = (wSum > 0) ? wNotUsed  / wSum : 1.0 / totalTiles;
 
  // ----- Buffer state -----
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
 
  // ----- Mosaic parameters (values used in VSE360) -----
  const double mu1 = 2.0, mu2 = 2.0, mu3 = 1.0, mu4 = 1.0;
 
  // rate of one tile at quality q in Mbps
  std::vector<double> rateMbps;
  for (uint32_t q = 0; q < perTileSizeBits.size(); ++q) {
    rateMbps.push_back(perTileSizeBits[q] / segmentDurSec / 1e6);
  }
 
  // ----- Search: one quality per category, viewport >= adjacent >= tier2 >= outside -----
  int highestQuality = m_qualityRates.size() - 1;
  int bestV = 0, bestA = 0, bestT = 0, bestO = 0;
  double bestQoE = -std::numeric_limits<double>::infinity();
  double bestQ = 0;
  bool found = false;
  for (int qV = highestQuality; qV >= 0; --qV) {
    for (int qA = qV; qA >= 0; --qA) {
      for (int qT = qA; qT >= 0; --qT) {
        for (int qO = qT; qO >= 0; --qO) {
          double extraBits = viewportTiles.size() * (perTileSizeBits[qV] - perTileSizeBits[0])
                           + adjacentTiles.size() * (perTileSizeBits[qA] - perTileSizeBits[0])
                           + tier2Tiles.size()    * (perTileSizeBits[qT] - perTileSizeBits[0])
                           + outsideTiles.size()  * (perTileSizeBits[qO] - perTileSizeBits[0]);
          if (extraBits > remainingBits) continue;
          double totalBits = totalLowestBits + extraBits;
 
          // Q: viewed quality
          double B = viewportTiles.size() * pViewport * rateMbps[qV]
                   + adjacentTiles.size() * pAdjacent * rateMbps[qA]
                   + tier2Tiles.size()    * pTier2    * rateMbps[qT]
                   + outsideTiles.size()  * pOutside  * rateMbps[qO]
                   + notUsed.size()       * pNotUsed  * rateMbps[0];
          double S = 0.0; // no tile is skipped
          double Q = 0.9 * B - 0.3 * S;
 
          // T: predicted stall
          double T = std::max(0.0, totalBits / netRate - bufferDuration);
 
          // G: switch from previous segment
          double G = (m_mosaicLastQ < 0) ? 0.0 : std::fabs(Q - m_mosaicLastQ);
 
          // E: std of p * rate over all tiles
          double xV = pViewport * rateMbps[qV], xA = pAdjacent * rateMbps[qA], xT = pTier2 * rateMbps[qT];
          double xO = pOutside * rateMbps[qO], xN = pNotUsed * rateMbps[0];
          double mean = (viewportTiles.size() * xV + adjacentTiles.size() * xA + tier2Tiles.size() * xT
                       + outsideTiles.size() * xO + notUsed.size() * xN) / totalTiles;
          double var = (viewportTiles.size() * std::pow(xV - mean, 2) + adjacentTiles.size() * std::pow(xA - mean, 2)
                      + tier2Tiles.size() * std::pow(xT - mean, 2) + outsideTiles.size() * std::pow(xO - mean, 2)
                      + notUsed.size() * std::pow(xN - mean, 2)) / totalTiles;
          double E = std::sqrt(var);
 
          double QoE = mu1 * Q - mu2 * T - mu3 * G - mu4 * E;
          if (QoE > bestQoE) {
            bestQoE = QoE;
            bestQ = Q;
            bestV = qV; bestA = qA; bestT = qT; bestO = qO;
            found = true;
          }
        }
      }
    }
  }
 
  if (found) {
    m_mosaicLastQ = bestQ;
  }
  for (uint32_t idx : viewportTiles) tileQualities[idx] = bestV;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = bestA;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = bestT;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = bestO;
 
  NS_LOG_INFO("Segment " << segmentId << ": Mosaic completed. QoE=" << bestQoE << " Q viewport/adjacent/tier2/outside = "
              << bestV << "/" << bestA << "/" << bestT << "/" << bestO);
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
///////////////////////2D Flat Video algorithms
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::Bola2DAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("Bola2DAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
 
  // ----- 2D state: throughput samples (SampleProcessor) and previous decision -----
  // Java sampleProcessor.dataNotAvailable(): no segment downloaded yet (first call of this algorithm)
  bool dataNotAvailable = (m_lastQuality2D < 0);
  if (!dataNotAvailable) {
    m_throughputHistory2D.push_back(netRate); // netRate = throughput sample of the last downloaded segment
  }
  int lastChunkIndex = m_throughputHistory2D.size(); // Java sampleProcessor.lastChunkIndex(): segments downloaded so far
  int Q = m_qualityRates.size();
 
  // ----- Buffer state -----
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
  const double maxBufferSec = 5.0; //  10.0
 
 
  // ===== BOLA (BolaTrackSelection.getMaxIndex) =====
  const int BOLA_STATE_STARTUP = 1;
  const int BOLA_STATE_STEADY = 2;
  const long MINIMUM_BUFFER_S = 2;                    // BOLA should never add artificial delays if buffer < MINIMUM_BUFFER_S, defualt- 10
  const long MINIMUM_BUFFER_PER_BITRATE_LEVEL_S = 2;
  const long DEFAULT_MIN_BUFFER_TIME = 2; //min time 12 default
  const double safetyFactor = 0.9;
 
  // utilitiesFromBitrates(): Java uses integer division bitrate/(8*1024) and offset = utility of
  // getFormat(0) = HIGHEST bitrate (Java index 0 = q = Q-1)
  std::vector<double> utilities(Q);
  double utilityOffset = std::log(std::floor(m_qualityRates[Q - 1] / (8 * 1024)));
  for (int q = 0; q < Q; ++q) {
    utilities[q] = std::log(std::floor(m_qualityRates[q] / (8 * 1024))) + utilityOffset;
  }
 
  // calculateBolaParameters(stableBufferTime, utilities)
  long stableBufferTime = DEFAULT_MIN_BUFFER_TIME;     // getStableBufferTime()
  int highestUtilityQ = Q - 1;                         // Java highestUtilityIndex / highestIndex = 0
  for (int q = Q - 1; q >= 0; --q) {                   // Java loop i = 0 .. length-1
    highestUtilityQ = utilities[q] > utilities[Q - 1] ? q : Q - 1;
  }
  long bufferTime = std::max(stableBufferTime, MINIMUM_BUFFER_S + MINIMUM_BUFFER_PER_BITRATE_LEVEL_S * (long) Q);
  double gp = (utilities[highestUtilityQ] - 1) / (double)(bufferTime / MINIMUM_BUFFER_S - 1); // long division as in Java
  double Vp = MINIMUM_BUFFER_S / gp;
 
  int targetQ = 0;
  if (dataNotAvailable) {
    targetQ = 0;                                       // lowestBitrateIndex()
  } else {
    // sampleProcessor.sampleAverage(5)
    uint32_t n = std::min<uint32_t>(5, m_throughputHistory2D.size());
    double sum = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
      sum += m_throughputHistory2D[m_throughputHistory2D.size() - 1 - i];
    }
    double throughputHistory = sum / n;
 
    double bufferS = std::floor(bufferDuration);       // Java bufferedDurationMs/1000 (long division)
 
    // findBestRateIndex(targetRate, safetyFactor): highest bitrate < safetyFactor*target, otherwise lowest
    int qualityForThroughput = 0;
    for (int q = Q - 1; q >= 0; --q) {
      if (m_qualityRates[q] < safetyFactor * throughputHistory) { qualityForThroughput = q; break; }
    }
 
    int quality = m_lastQuality2D;
    double delayS = 0;
    switch (m_bolaState2D) {
      case BOLA_STATE_STARTUP:
      {
        quality = qualityForThroughput;
 
        // minBufferLevelForQuality(quality): Java loop i = quality-1 .. 0 = higher bitrates (q+1 .. Q-1)
        double minLevel = 0;
        for (int qi = quality + 1; qi <= Q - 1; ++qi) {
          if (utilities[qi] < utilities[quality]) {
            double level = Vp * (gp + (m_qualityRates[quality] * utilities[qi] - m_qualityRates[qi] * utilities[quality])
                                      / (m_qualityRates[quality] - m_qualityRates[qi]));
            minLevel = std::max(minLevel, level); // we want min to be small but at least level(i) for all i
          }
        }
        m_bolaPlaceholderBuffer2D = std::max(0.0, minLevel - bufferS);
 
        if (segmentDurSec > 0 && bufferS >= std::floor(segmentDurSec)) {
          m_bolaState2D = BOLA_STATE_STEADY;
        }
        break;
      }
      case BOLA_STATE_STEADY:
      {
        double bufferLevel = bufferS + m_bolaPlaceholderBuffer2D;
 
        // getQualityFromBufferLevel(bufferLevel): Java loop i = 0 .. length-1 (highest -> lowest)
        int bolaQ = Q - 1;
        double score = 0;
        for (int q = Q - 1; q >= 0; --q) {
          double s = (Vp * (utilities[q] + gp) - bufferLevel) / m_qualityRates[q];
          if (score == 0 || s >= score) {
            score = s;
            bolaQ = q;
          }
        }
        quality = bolaQ;
 
        // Java compares the (decreasing-order) index with lastChunkIndex; kept as in Java
        int jQuality = Q - 1 - quality;
        int jQualityForThroughput = Q - 1 - qualityForThroughput;
        if (jQuality > lastChunkIndex && jQuality > jQualityForThroughput) {
          jQuality = std::max(jQualityForThroughput, lastChunkIndex);
          quality = Q - 1 - jQuality;
        }
 
        // maxBufferLevelForQuality(quality)
        delayS = std::max(0.0, bufferLevel - Vp * (utilities[quality] + gp));
 
        if (delayS <= m_bolaPlaceholderBuffer2D) {
          m_bolaPlaceholderBuffer2D -= delayS;
          delayS = 0;
        } else {
          delayS -= m_bolaPlaceholderBuffer2D;
          m_bolaPlaceholderBuffer2D = 0;
          if (jQuality < lastChunkIndex) {
            // At top quality, allow schedule controller to decide how far to fill buffer.
            delayS = 1000 * delayS;
          } else {
            delayS = 0;
          }
        }
        break; // BOLA_STATE_STEADY
      }
      default:
        m_bolaState2D = BOLA_STATE_STARTUP;
    }
    long delayms = (long) delayS;
    // Java notifyDelayListener(): the engine cannot postpone requests, so the delay is only reported
    NS_LOG_DEBUG("Bola2D: state=" << m_bolaState2D << " placeholder=" << m_bolaPlaceholderBuffer2D
                 << "s delay=" << delayms << "ms selected quality=" << quality);
    targetQ = quality;
  }
 
 
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    m_lastQuality2D = targetQ;
    m_lastSegmentBits2D = totalLowestBits;
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- Apply the 2D quality to the tiles -----
  // In 2D every tile is part of the picture, so every category gets targetQ.
  // Categories are processed in priority order: if the budget is short, the FoV order decides
  // which tiles keep targetQ and which are lowered first.
  std::vector<std::pair<std::vector<uint32_t>*, std::string>> categories = {
    {&viewportTiles, "viewport"},
    {&adjacentTiles, "adjacent"},
    {&tier2Tiles,   "tier2"},
    {&outsideTiles, "outside"},
    {&notUsed,      "notUsed"}   // lowest priority
  };
 
  for (uint32_t c = 0; c < categories.size(); ++c) {
    std::vector<uint32_t>* tilesVec = categories[c].first;
    std::string catName = categories[c].second;
    uint32_t numTiles = tilesVec->size();
    if (numTiles == 0) continue;
 
    int bestQ = targetQ;
    // Lower the Bola quality until the upgrade of the whole category fits
    while (bestQ > 0 && numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]) > remainingBits) {
      bestQ--;
    }
 
    if (bestQ > 0) {
      double usedBits = numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]);
      remainingBits -= usedBits;
      NS_LOG_DEBUG("Category " << catName << ": Bola2D quality " << bestQ << ", used extra "
                   << usedBits << " bits, remaining " << remainingBits);
      for (uint32_t idx : *tilesVec) {
        tileQualities[idx] = bestQ;
      }
    } else {
      NS_LOG_DEBUG("Category " << catName << ": staying at quality 0");
    }
  }
 
  // ----- Save state for the next segment -----
  m_lastQuality2D = targetQ;                         // Java selectedIndex / lastRepLevel
  m_lastSegmentBits2D = budgetBits - remainingBits;  // bits requested for this segment
 
  NS_LOG_INFO("Segment " << segmentId << ": Bola2D completed. Target quality " << targetQ
              << ", buffer " << bufferDuration << "s");
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//BBA 2D- BBA2D with BB1 linear map
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::Bba2_2DAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("Bba2_2DAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
 
  // ----- 2D state: throughput samples (SampleProcessor) and previous decision -----
  // Java sampleProcessor.dataNotAvailable(): no segment downloaded yet (first call of this algorithm)
  bool dataNotAvailable = (m_lastQuality2D < 0);
  if (!dataNotAvailable) {
    m_throughputHistory2D.push_back(netRate); // netRate = throughput sample of the last downloaded segment
  }
  int lastChunkIndex = m_throughputHistory2D.size(); // Java sampleProcessor.lastChunkIndex(): segments downloaded so far
  int Q = m_qualityRates.size();
 
  // ----- Buffer state -----
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
  const double maxBufferSec = 5.0; // Java sampleProcessor.maxBufferMs() (same value as in ApplyBufferAwareAdjustments), was 10
 
  // Java sampleProcessor.lastSampleDurationMs(): download time of the last segment = its size / measured throughput
  double lastDownloadTimeSec = (netRate > 0) ? m_lastSegmentBits2D / netRate : 0.0;
 
 
  // ===== BBA2 (Bba2TrackSelection.calculateSelectedIndex) =====
  int targetQ = 0;
  if (dataNotAvailable) {
    targetQ = 0;                                       // lowestBitrateIndex()
  } else {
    const double lastChunkDurationSec = segmentDurSec; // sampleProcessor.lastChunkDurationMs()
    int lastRateQ = m_lastQuality2D;                   // getRepIndex(lastRepLevel())
    double lastRate = m_qualityRates[lastRateQ];       // sampleProcessor.lastRepLevel()
    int retQ;
    int qRateQ = 0;                                    // qRateIndex = lowestBitrateIndex()
 
    // --- bba1UpdateResevoir(lastRate, lastRateIndex) ---
    // resvWin = min(2*maxBuffer/chunkDur, segmentsLeftInVideo). The engine does not know the video
    // length, so only the first term is used.
    long resvWin = (long) std::floor(2 * maxBufferSec / lastChunkDurationSec);
    double avgSegSize = (lastRate * lastChunkDurationSec) / 8;   // bytes
    double largeChunks = 0;
    double smallChunks = 0;
    for (long i = 0; i < resvWin; i++) {
      // futureChunkInfo.getByteSize(lastChunkIndex + i, lastRateIndex): CBR segments in this client,
      // so every future segment at lastRateQ has size m_qualityRates[lastRateQ] * segDur / 8.
      double futureSegSize = (m_qualityRates[lastRateQ] * lastChunkDurationSec) / 8;
      if (futureSegSize > avgSegSize)
        largeChunks += futureSegSize;
      else
        smallChunks += futureSegSize;
    }
    double reservoir = 8 * (largeChunks - smallChunks) / lastRate;   // seconds
    if (reservoir < (2 * lastChunkDurationSec))
      reservoir = 2 * lastChunkDurationSec;
    else {
      if (reservoir > 0.6 * maxBufferSec)
        reservoir = 0.6 * maxBufferSec;
    }
 
    double SFT = lastDownloadTimeSec;                  // sampleProcessor.lastSampleDurationMs()
    if (SFT > lastChunkDurationSec)
      m_bba2StaticAlgPar2D = 1;                        // switch to BBA1 if buffer is decreasing
 
    if (bufferDuration < reservoir) {                  // CHECK BUFFER LEVEL
      if (m_bba2StaticAlgPar2D != 0) {
        retQ = 0;                                      // lowestBitrateIndex()
      } else {
        // start up phase
        if (SFT < 0.125 * lastChunkDurationSec) {
          // buffer level increasing fast
          retQ = std::min(lastRateQ + 1, Q - 1);       // Java max(lastRateIndex - 1, 0)
        } else {
          retQ = lastRateQ;
        }
      }
    } else {
      // --- bba1VRAA(lastRateIndex, reservoir) ---
      int rateUQ = std::min(lastRateQ + 1, Q - 1);     // Java rateUindex = max(lastRateIndex - 1, 0)
      int rateLQ = lastRateQ - 1;                      // Java rateLindex = min(lastRateIndex + 1, tracks.length)
      int optQ;
      if (bufferDuration < reservoir) {
        optQ = -1;                                     // Java optRateIndex = tracks.length
      } else if (bufferDuration > 0.9 * maxBufferSec) {
        optQ = Q - 1;                                  // Java optRateIndex = 0 (highest)
      } else {
        double low = m_qualityRates[0];                // lowestBitrate()
        double high = m_qualityRates[Q - 1];           // highestBitrate()
        double slope = (high - low) / (0.9 * maxBufferSec - reservoir);
        double rate = low + slope * (bufferDuration - reservoir);
        optQ = 0;                                      // findBestRateIndex(rate)
        for (int q = Q - 1; q >= 0; --q) {
          if (m_qualityRates[q] < rate) { optQ = q; break; }
        }
      }
      int bba1RateQ;
      if (optQ == -1 || optQ == Q - 1)
        bba1RateQ = std::max(optQ, 0);                 // optQ == -1 is unreachable (called only when buffer >= reservoir)
      else if (optQ >= rateUQ)                         // Java optRateIndex <= rateUindex
        bba1RateQ = optQ;
      else if (optQ <= rateLQ)                         // Java optRateIndex >= rateLindex
        bba1RateQ = optQ + 1;                          // Java optRateIndex - 1
      else
        bba1RateQ = lastRateQ;
 
      if (m_bba2StaticAlgPar2D != 0) {
        // execute BBA1
        retQ = bba1RateQ;
      } else { // beyond reservoir
        if (SFT <= 0.5 * lastChunkDurationSec) {
          // buffer level increasing fast
          qRateQ = std::min(lastRateQ + 1, Q - 1);     // Java max(lastRateIndex - 1, 0)
        }
        retQ = std::max(bba1RateQ, qRateQ);            // Java min(bba1RateIndex, qRateIndex)
 
        if (bba1RateQ > qRateQ) {                      // Java bba1RateIndex < qRateIndex
          m_bba2StaticAlgPar2D = 1;
        }
      }
    }
    targetQ = retQ;
    NS_LOG_DEBUG("Bba2_2D: reservoir=" << reservoir << "s SFT=" << SFT << "s BBA1 mode=" << m_bba2StaticAlgPar2D
                 << " selected quality=" << targetQ);
  }
 
 
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    m_lastQuality2D = targetQ;
    m_lastSegmentBits2D = totalLowestBits;
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- Apply the 2D quality to the tiles -----
  // In 2D every tile is part of the picture, so every category gets targetQ.
  // Categories are processed in priority order: if the budget is short, the FoV order decides
  // which tiles keep targetQ and which are lowered first.
  std::vector<std::pair<std::vector<uint32_t>*, std::string>> categories = {
    {&viewportTiles, "viewport"},
    {&adjacentTiles, "adjacent"},
    {&tier2Tiles,   "tier2"},
    {&outsideTiles, "outside"},
    {&notUsed,      "notUsed"}   // lowest priority
  };
 
  for (uint32_t c = 0; c < categories.size(); ++c) {
    std::vector<uint32_t>* tilesVec = categories[c].first;
    std::string catName = categories[c].second;
    uint32_t numTiles = tilesVec->size();
    if (numTiles == 0) continue;
 
    int bestQ = targetQ;
    // Lower the Bba2_ quality until the upgrade of the whole category fits
    while (bestQ > 0 && numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]) > remainingBits) {
      bestQ--;
    }
 
    if (bestQ > 0) {
      double usedBits = numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]);
      remainingBits -= usedBits;
      NS_LOG_DEBUG("Category " << catName << ": Bba2_2D quality " << bestQ << ", used extra "
                   << usedBits << " bits, remaining " << remainingBits);
      for (uint32_t idx : *tilesVec) {
        tileQualities[idx] = bestQ;
      }
    } else {
      NS_LOG_DEBUG("Category " << catName << ": staying at quality 0");
    }
  }
 
  // ----- Save state for the next segment -----
  m_lastQuality2D = targetQ;                         // Java selectedIndex / lastRepLevel
  m_lastSegmentBits2D = budgetBits - remainingBits;  // bits requested for this segment
 
  NS_LOG_INFO("Segment " << segmentId << ": Bba2_2D completed. Target quality " << targetQ
              << ", buffer " << bufferDuration << "s");
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//Elastic 2D
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::Elastic2DAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("Elastic2DAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
 
  // ----- 2D state: throughput samples (SampleProcessor) and previous decision -----
  // Java sampleProcessor.dataNotAvailable(): no segment downloaded yet (first call of this algorithm)
  bool dataNotAvailable = (m_lastQuality2D < 0);
  if (!dataNotAvailable) {
    m_throughputHistory2D.push_back(netRate); // netRate = throughput sample of the last downloaded segment
  }
  int lastChunkIndex = m_throughputHistory2D.size(); // Java sampleProcessor.lastChunkIndex(): segments downloaded so far
  int Q = m_qualityRates.size();
 
  // ----- Buffer state -----
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
  const double maxBufferSec = 5.0; // was 10
 
  // Java sampleProcessor.lastSampleDurationMs(): download time of the last segment = its size / measured throughput
  double lastDownloadTimeSec = (netRate > 0) ? m_lastSegmentBits2D / netRate : 0.0;
 
 
  // ===== Elastic (ElasticTrackSelection.updateSelectedTrack / calculateSelectedIndex) =====
  const int elasticAverageWindow = 5;   // DEFAULT_ELASTIC_AVERAGE_WINDOW
  const double k_p = 0.01;              // DEFAULT_K_P
  const double k_i = 0.001;             // DEFAULT_K_I
 
  int targetQ = 0;
  if (dataNotAvailable) {
    targetQ = 0;                         // lowestBitrateIndex()
  } else if (lastChunkIndex <= 1) {
    targetQ = 0;                         // lowestBitrateIndex()
    NS_LOG_DEBUG("Elastic2D: Not yet enter");
  } else {
    // sampleProcessor.sampleHarmonicAverage(elasticAverageWindow)
    uint32_t n = std::min<uint32_t>(elasticAverageWindow, m_throughputHistory2D.size());
    double invSum = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
      invSum += 1.0 / m_throughputHistory2D[m_throughputHistory2D.size() - 1 - i];
    }
    double averageRateEstimate = n / invSum;
 
    const double downloadTimeS = lastDownloadTimeSec;
    const double maxBufferS = maxBufferSec;
    const double bufferedDurationS = bufferDuration;
 
    m_elasticStaticAlgParameter2D += downloadTimeS * (bufferedDurationS - maxBufferS);
    double targetRate = averageRateEstimate / (1 - k_p * bufferedDurationS - k_i * m_elasticStaticAlgParameter2D);
    if (targetRate <= 0) {
      targetRate = 0;
    }
    NS_LOG_DEBUG("Elastic2D: avgRate=" << averageRateEstimate << " targetRate = " << targetRate / 1000 << " kbps");
 
    // findBestRateIndex(targetRate): highest bitrate < target (strict, as in AlgorithmTrackSelection), otherwise lowest
    targetQ = 0;
    for (int q = Q - 1; q >= 0; --q) {
      if (m_qualityRates[q] < targetRate) { targetQ = q; break; }
    }
  }
  NS_LOG_DEBUG("Elastic2D: Selected quality = " << targetQ);
 
 
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    m_lastQuality2D = targetQ;
    m_lastSegmentBits2D = totalLowestBits;
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- Apply the 2D quality to the tiles -----
  // In 2D every tile is part of the picture, so every category gets targetQ.
  // Categories are processed in priority order: if the budget is short, the FoV order decides
  // which tiles keep targetQ and which are lowered first.
  std::vector<std::pair<std::vector<uint32_t>*, std::string>> categories = {
    {&viewportTiles, "viewport"},
    {&adjacentTiles, "adjacent"},
    {&tier2Tiles,   "tier2"},
    {&outsideTiles, "outside"},
    {&notUsed,      "notUsed"}   // lowest priority
  };
 
  for (uint32_t c = 0; c < categories.size(); ++c) {
    std::vector<uint32_t>* tilesVec = categories[c].first;
    std::string catName = categories[c].second;
    uint32_t numTiles = tilesVec->size();
    if (numTiles == 0) continue;
 
    int bestQ = targetQ;
    // Lower the Elastic quality until the upgrade of the whole category fits
    while (bestQ > 0 && numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]) > remainingBits) {
      bestQ--;
    }
 
    if (bestQ > 0) {
      double usedBits = numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]);
      remainingBits -= usedBits;
      NS_LOG_DEBUG("Category " << catName << ": Elastic2D quality " << bestQ << ", used extra "
                   << usedBits << " bits, remaining " << remainingBits);
      for (uint32_t idx : *tilesVec) {
        tileQualities[idx] = bestQ;
      }
    } else {
      NS_LOG_DEBUG("Category " << catName << ": staying at quality 0");
    }
  }
 
  // ----- Save state for the next segment -----
  m_lastQuality2D = targetQ;                         // Java selectedIndex / lastRepLevel
  m_lastSegmentBits2D = budgetBits - remainingBits;  // bits requested for this segment
 
  NS_LOG_INFO("Segment " << segmentId << ": Elastic2D completed. Target quality " << targetQ
              << ", buffer " << bufferDuration << "s");
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}
//Logistic 2D
std::map<uint32_t, std::vector<int>>
TiledDashClientAbrEngine::Logistic2DAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline)
{
  NS_LOG_FUNCTION (this << netRate << segmentId << deadline.GetSeconds());
  std::map<uint32_t, std::vector<int>> qualityMap;
  std::vector<int> tileQualities (fovScores.size(), -1);
 
  // ----- Deadline safeguard (same as ViewportAware) -----
  if (deadline <= Time(0)) {
    NS_LOG_WARN("Deadline is zero or negative. Using minimum quality for all needed tiles.");
    for (uint32_t i = 0; i < fovScores.size(); ++i) {
      if (fovScores[i] >= 0) tileQualities[i] = 0;
    }
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // Categorize tiles (viewport=0, adjacent=1, tier2=2, outside=3, notUsed=-1)
  std::vector<uint32_t> viewportTiles, adjacentTiles, tier2Tiles, outsideTiles, notUsed;
  for (uint32_t i = 0; i < fovScores.size(); ++i) {
    if (fovScores[i] == 0)      viewportTiles.push_back(i);
    else if (fovScores[i] == 1) adjacentTiles.push_back(i);
    else if (fovScores[i] == 2) tier2Tiles.push_back(i);
    else if (fovScores[i] == 3) outsideTiles.push_back(i);
    else notUsed.push_back(i); // fovScores[i] == -1
  }
 
  uint32_t totalTiles = viewportTiles.size() + adjacentTiles.size() + tier2Tiles.size() + outsideTiles.size() + notUsed.size();
  if (totalTiles == 0) {
    NS_LOG_INFO("No tiles needed for segment " << segmentId);
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Dynamically update minTiles (same as ViewportOnlyAdaptQuality) -----
  uint32_t numViewportTiles = viewportTiles.size();
  if (m_client && m_client->GetMediaBuffer()) {
    uint32_t newMinTiles = std::max(m_client->GetMediaBuffer()->GetMinTiles(), numViewportTiles);
    m_client->GetMediaBuffer()->SetMinTiles(newMinTiles);
    NS_LOG_INFO("Logistic2DAdaptQuality: updated minTiles to " << newMinTiles
                << " (viewport tiles = " << numViewportTiles << ")");
  }
 
  // ----- Pre-compute per-tile sizes for each quality (bits) -----
  double segmentDurSec = m_client->GetSegmentDuration().GetSeconds();
  const double T_TOTAL = m_client->GetHorTiles() * m_client->GetVerTiles();
  std::vector<double> perTileSizeBits;
  for (double rateBps : m_qualityRates) {
    perTileSizeBits.push_back((rateBps * segmentDurSec) / T_TOTAL);
  }
 
  // ----- Budget: netRate * segment duration -----
  double budgetBits = netRate * segmentDurSec;
  NS_LOG_DEBUG("Budget: " << budgetBits << " bits");
 
  // ----- Step 1: assign lowest quality (0) to ALL tiles (including notUsed) -----
  for (uint32_t idx : viewportTiles) tileQualities[idx] = 0;
  for (uint32_t idx : adjacentTiles) tileQualities[idx] = 0;
  for (uint32_t idx : tier2Tiles)   tileQualities[idx] = 0;
  for (uint32_t idx : outsideTiles) tileQualities[idx] = 0;
  for (uint32_t idx : notUsed)      tileQualities[idx] = 0;  // also download at lowest quality
 
  // Total cost of all tiles at quality 0
  double totalLowestBits = totalTiles * perTileSizeBits[0];
 
  // ----- 2D state: throughput samples (SampleProcessor) and previous decision -----
  // Java sampleProcessor.dataNotAvailable(): no segment downloaded yet (first call of this algorithm)
  bool dataNotAvailable = (m_lastQuality2D < 0);
  if (!dataNotAvailable) {
    m_throughputHistory2D.push_back(netRate); // netRate = throughput sample of the last downloaded segment
  }
  int lastChunkIndex = m_throughputHistory2D.size(); // Java sampleProcessor.lastChunkIndex(): segments downloaded so far
  int Q = m_qualityRates.size();
 
  // ----- Buffer state -----
  double bufferDuration = m_client->GetMediaBuffer()->GetCurrentBufferDuration().GetSeconds();
  const double maxBufferSec = 10.0; // Java sampleProcessor.maxBufferMs() (same value as in ApplyBufferAwareAdjustments)
 
 
  // ===== Logistic (LogisticTrackSelector.logisticFunction) =====
  int targetQ = 0;
  if (dataNotAvailable) {
    targetQ = 0;                                       // lowestBitrateIndex()
  } else {
    int lastRateQ = m_lastQuality2D;                   // getRepIndex(lastRepLevel())
    long bufferedDurationMs = (long)(bufferDuration * 1000);
    long maxBufferMs = (long)(maxBufferSec * 1000);
    int rateUQ = std::min(lastRateQ + 1, Q - 1);       // Java rateUindex = max(lastRateIndex - 1, 0)
    int rateLQ = lastRateQ - 1;                        // Java rateLindex = min(lastRateIndex + 1, tracks.length)
    int optQ;
    if (bufferedDurationMs >= (long)(0.97 * maxBufferMs)) {
      optQ = Q - 1;                                    // Java optRateIndex = 0 (highest)
    } else {
      long low = (long) m_qualityRates[0] / 1024;      // lowestBitrate()/1024 (int division as in Java)
      long high = (long) m_qualityRates[Q - 1] / 1024; // highestBitrate()/1024
      long targetRate = (long)(std::ceil(high / (1 + ((high / (low) - 1) * std::exp(-0.05 * bufferedDurationMs / 1000)))));
      NS_LOG_DEBUG("Logistic2D: target rate " << targetRate << " kbit/s (x1024)");
      optQ = 0;                                        // findBestRateIndex(targetRate*1024)
      for (int q = Q - 1; q >= 0; --q) {
        if (m_qualityRates[q] < targetRate * 1024) { optQ = q; break; }
      }
    }
 
    if (optQ == Q - 1)                                 // Java optRateIndex == 0 (== tracks.length never happens)
      targetQ = optQ;
    else if (optQ >= rateUQ)                           // Java optRateIndex <= rateUindex
      targetQ = optQ;
    else if (optQ <= rateLQ)                           // Java optRateIndex >= rateLindex
      targetQ = optQ + 1;                              // Java optRateIndex - 1
    else
      targetQ = lastRateQ;
    NS_LOG_DEBUG("Logistic2D: Selected quality = " << targetQ);
  }
 
 
  if (totalLowestBits > budgetBits) {
    NS_LOG_WARN("Even lowest quality exceeds budget. Keeping lowest quality.");
    m_lastQuality2D = targetQ;
    m_lastSegmentBits2D = totalLowestBits;
    qualityMap[segmentId] = tileQualities;
    return qualityMap;
  }
 
  // ----- Available extra budget above the baseline (quality 0) -----
  double remainingBits = budgetBits - totalLowestBits;
 
  // ----- Apply the 2D quality to the tiles -----
  // In 2D every tile is part of the picture, so every category gets targetQ.
  // Categories are processed in priority order: if the budget is short, the FoV order decides
  // which tiles keep targetQ and which are lowered first.
  std::vector<std::pair<std::vector<uint32_t>*, std::string>> categories = {
    {&viewportTiles, "viewport"},
    {&adjacentTiles, "adjacent"},
    {&tier2Tiles,   "tier2"},
    {&outsideTiles, "outside"},
    {&notUsed,      "notUsed"}   // lowest priority
  };
 
  for (uint32_t c = 0; c < categories.size(); ++c) {
    std::vector<uint32_t>* tilesVec = categories[c].first;
    std::string catName = categories[c].second;
    uint32_t numTiles = tilesVec->size();
    if (numTiles == 0) continue;
 
    int bestQ = targetQ;
    // Lower the Logistic quality until the upgrade of the whole category fits
    while (bestQ > 0 && numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]) > remainingBits) {
      bestQ--;
    }
 
    if (bestQ > 0) {
      double usedBits = numTiles * (perTileSizeBits[bestQ] - perTileSizeBits[0]);
      remainingBits -= usedBits;
      NS_LOG_DEBUG("Category " << catName << ": Logistic2D quality " << bestQ << ", used extra "
                   << usedBits << " bits, remaining " << remainingBits);
      for (uint32_t idx : *tilesVec) {
        tileQualities[idx] = bestQ;
      }
    } else {
      NS_LOG_DEBUG("Category " << catName << ": staying at quality 0");
    }
  }
 
  // ----- Save state for the next segment -----
  m_lastQuality2D = targetQ;                         // Java selectedIndex / lastRepLevel
  m_lastSegmentBits2D = budgetBits - remainingBits;  // bits requested for this segment
 
  NS_LOG_INFO("Segment " << segmentId << ": Logistic2D completed. Target quality " << targetQ
              << ", buffer " << bufferDuration << "s");
  qualityMap[segmentId] = tileQualities;
  return qualityMap;
}

} // namespace ns3


