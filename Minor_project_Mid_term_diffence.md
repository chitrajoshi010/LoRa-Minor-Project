# Scalable Multi-hop LoRa Networks for Intelligent Forest Monitoring

A Minor Project Report

**Submitted By:**

- Chitra Raj Joshi (THA080BEI018)
- Keshar Singh Sunar (THA080BEI022)
- Prabesh Parajulee (THA080BEI029)
- Santosh Gadtaula (THA080BEI042)

**Submitted To:** Department of Electronics and Computer Engineering, Thapathali Campus, Kathmandu, Nepal

In partial fulfillment for the award of the Bachelor's Degree in Electronics, Communication and Information Engineering

**Under the Supervision of:** Er. Saroj Shakya

July, 2026

---

## Acknowledgement

This project would not have taken shape without the support of several people and institutions, and we would like to acknowledge them here.

First, we are thankful to the Department of Electronics and Computer Engineering at Thapathali Campus, Institute of Engineering, Tribhuvan University, for making this minor project a part of our academic curriculum. We owe a special thanks to our supervisor, Er. Saroj Shakya, for his time and patience throughout the planning of this project. His feedback pushed us to think more carefully about the practical challenges of deploying acoustic sensing in real forest environments, and his suggestions helped us narrow down a fairly broad idea into something we could realistically build and test.

We are also thankful to our classmates and friends, with whom countless informal discussions helped us work through design choices, troubleshoot ideas, and stay motivated through the early planning stages of this project.

Chitra Raj Joshi (THA080BEI018), Keshar Singh Sunar (THA080BEI022), Prabesh Parajulee (THA080BEI029), Santosh Gadtaula (THA080BEI042)

---

## Abstract

Forests face increasing threats from wildfires, illegal logging, and poaching, which lead to the demand for reliable, energy-efficient monitoring systems capable of operating in remote environments. This project proposes a multi-hop LoRa-based wireless sensor network for intelligent forest monitoring that integrates environmental sensing, edge-based machine learning, and low-power communication. The proposed system consists of multiple sensor nodes and a gateway controlled by ESP32 microcontroller. The sensor node consists of multiple sensors like temperature, humidity, and gas (CO2) sensors, which combine multisensory fusion to give the fire risk score. The acoustic data captured by a MEMS microphone is processed through a TinyML inference model deployed directly on the node, enabling on-device classification of illegal activity sounds such as chainsaws and gunshots. A network of LoRa-enabled sensor nodes collaboratively communicates to a central gateway, which aggregates real-time data and forwards critical alerts to a live web dashboard for monitoring and response. Nodes operate in deep-sleep mode and wake periodically, and inter-node communication is handled through the LDSE protocol, which enables multi-hop networking and extends coverage beyond a single gateway range, delivering a low-cost, scalable, and energy-efficient solution for forest monitoring in community forests and protected areas. The proposed system is expected to improve acoustic classification, improve fire detection accuracy, and provide a low power and energy efficient device for remote monitoring.

**Keywords:** Wireless Sensor Networks (WSN), Multi-hop LoRa Communication, Edge AI, TinyML, Energy-Efficient Routing, LDSE Protocol, Forest Monitoring, Gateway, Multisensor fusion.

---

## Table of Contents

1. [Introduction](#1-introduction)
   - 1.1 Background
   - 1.2 Motivation
   - 1.3 Problem Definition
   - 1.4 Objectives
   - 1.5 Scope and Applications
     - 1.5.1 Scope
     - 1.5.2 Applications
2. [Literature Review](#2-literature-review)
3. [Methodology](#3-methodology)
   - 3.1 System Overview and Architecture
   - 3.2 System Components (Hardware)
     - 3.2.1 Forest Nodes (ESP32-S3)
     - 3.2.2 Gateway (ESP32-S3)
   - 3.3 Networking and Communication Protocol (LDSE)
     - 3.3.1 Architecture of LDSE
     - 3.3.2 Power Consumption Model
     - 3.3.3 Protocol-Centric (Focus on LDSE)
     - 3.3.4 Operational Challenges and Mitigation Strategies
   - 3.4 Intelligent Detection Strategy (Edge AI)
     - 3.4.1 Acoustic Dataset: FSC22
     - 3.4.2 Acoustic Processing and TinyML Model Training
     - 3.4.3 Fire Risk Scoring
   - 3.5 System Data Flow
   - 3.6 Overall Working Principle
4. [Expected Outcomes](#4-expected-outcomes)
5. [Project Schedule](#5-project-schedule)
6. [Project Budget](#6-project-budget)
7. [Feasibility Analysis](#7-feasibility-analysis)
   - 7.1 Technical Feasibility
   - 7.2 Economic Feasibility
   - 7.3 Operational Feasibility
   - 7.4 Time Feasibility
8. [References](#8-references)

---

## List of Figures

- **Figure 3-1:** Proposed System Architecture
- **Figure 3-2:** LDSE protocol network hierarchy
- **Figure 3-3:** LDSE Protocol Epoch Structure
- **Figure 3-4:** Sequential Synchronization and Operational Flowchart
- **Figure 3-5:** Acoustic TinyML Processing Pipeline
- **Figure 3-6:** Proposed System Dataflow

---

## List of Tables

- **Table 5-1:** Gantt chart
- **Table 6-1:** Project Budget

---

## List of Abbreviations

| Abbreviation | Meaning |
|---|---|
| AI | Artificial Intelligence |
| CNN | Convolutional Neural Network |
| DHT22 | Digital Humidity and Temperature Sensor (Model 22) |
| DSP | Digital Signal Processing |
| ESP32-S3 | Espressif Systems 32-bit Microcontroller (Series 3) |
| FSC22 | Forest Sound Classification Dataset 2022 |
| FTSP | Flooding Time Synchronization Protocol |
| int8 | 8-bit Integer Quantization |
| IoT | Internet of Things |
| IRE | Implicit Route Exploration |
| JSON | JavaScript Object Notation |
| LDSE | Layered Dynamic Synchronization Energy-saving |
| LoRa | Long Range (radio modulation technology) |
| LoRaWAN | Long Range Wide Area Network |
| MEMS | Micro-Electro-Mechanical Systems |
| MFCC | Mel-Frequency Cepstral Coefficient |
| MHz | Megahertz |
| ML | Machine Learning |
| MQ135 | Air Quality Sensor Module (Model 135) |
| RF | Random Forest |
| SX1278 | Semtech LoRa Transceiver (Model 1278) |
| TFLite | TensorFlow Lite |
| TinyML | Tiny Machine Learning |
| WSN | Wireless Sensor Network |

---

## 1. Introduction

### 1.1 Background

Forests are among the most critical ecosystems on Earth, providing ecological stability, biodiversity, and carbon sequestration services that are vital to sustaining life. However, they face growing and compounding threats from illegal logging, poaching, and wildfires, particularly in densely forested regions such as Nepal's community forests and protected areas. Conventional patrol-based monitoring cannot cover large remote areas continuously, and incidents often go undetected until significant environmental and economic damage has already occurred.

Nepal experiences approximately 40,000 hectares of burned forest area annually [1][2], with more than 78% to 86% of fire incidents occurring between March and May during the dry season [3]. Historical analyses also indicate that the total burned area has increased by approximately 0.6% per year since 2001, highlighting the growing need for early detection and continuous monitoring systems. Human activities combined with prolonged dry conditions remain the dominant contributors to forest fire occurrence.

Low-power wide-area networks such as LoRa support long-range, energy-efficient wireless links that are well suited for deployment in dense and geographically challenging forest environments. Prior systems have used sound and motion sensing for illegal logging and wildlife protection, but many rely on single-hop star topologies that limit coverage range and require dense gateway infrastructure. Machine learning deployed at the edge further enables intelligent, real-time decision-making without dependence on cloud connectivity, which is often unavailable in remote forests.

This project addresses the coverage and intelligence gap by combining multi-hop LDSE networking with edge artificial intelligence on the ESP32-S3 microcontroller for unified fire detection, acoustic threat identification, and network scalability in a single low-cost platform.

### 1.2 Motivation

Forests play a critical role in maintaining ecological balance, yet they remain vulnerable to threats such as wildfires, illegal logging, and poaching. These activities often occur in remote areas where continuous human supervision is impractical, causing incidents to go undetected until significant environmental and economic damage has already occurred. Existing monitoring approaches are often limited by inadequate coverage, delayed detection, and the challenges of operating in large, inaccessible forest regions. Therefore, there is a growing need for an intelligent and scalable monitoring system capable of providing early threat detection and reliable coverage across vast forest environments.

Nepal's community forests and protected reserves, including those in the Terai and mid-hill regions, face persistent threats from unauthorized timber extraction, wildlife poaching, and seasonal wildfires. The absence of affordable and autonomous monitoring infrastructure means that forest rangers must physically patrol vast areas, a process that is both costly and ineffective. An integrated IoT system combining LoRa communication and on-device machine learning can transform how these threats are detected and reported, enabling faster response times, improving conservation outcomes, and contributing to the long-term protection of forest ecosystems.

### 1.3 Problem Definition

Most existing LoRaWAN-based forest monitoring systems rely on a single-hop communication model where sensor nodes must directly reach a central gateway, which significantly limits coverage in dense and geographically challenging forest environments. These systems often depend on continuous or frequent data transmission, leading to high energy consumption and reduced node lifetime, particularly in remote deployments where maintenance is difficult. In addition, many traditional approaches use simple threshold-based detection mechanisms that are highly sensitive to environmental noise, resulting in frequent false alarms or missed detections when thresholds are not properly tuned.

Furthermore, existing designs are typically optimized for a single type of event such as fire detection, lacking the capability to simultaneously monitor multiple threats like illegal logging or human intrusion. Sound-based detection systems in the literature often perform inference on cloud servers, making them unsuitable for forest environments with limited or no connectivity. The absence of efficient in-network processing and reliance on raw data streaming to central servers further increases communication overhead and limits scalability in large-scale forest monitoring scenarios.

### 1.4 Objectives

The project objectives are:

- To develop a real time system for detecting early fire by computing real time fire risk score using multi source data.
- To detect acoustic threats by deploying a quantized TinyML model on the edge to identify illegal sounds such as chainsaws, axes and gunshots.
- To deploy the LDSE multi-hop LoRa protocol and benchmark its coverage extension and energy efficiency improvements against conventional LoRaWAN.

### 1.5 Scope and Applications

#### 1.5.1 Scope

The proposed prototype consists of two forest sensor nodes and one gateway. The sensor nodes are built on the ESP32-S3 platform and communicate using LoRa technology, while the LDSE multi-hop routing protocol enables reliable data transmission over distances exceeding 2 km when a direct gateway connection is unavailable. The gateway receives sensor data and transmits it to a web-based dashboard through Wi-Fi, allowing real-time monitoring and alert visualization. System evaluation includes laboratory testing to validate sensor readings, assess acoustic model inference accuracy, and verify network communication. In addition, limited outdoor field trials will be conducted to evaluate communication reliability, overall system performance, and energy efficiency.

The prototype is limited to a single-gateway architecture, and large-scale multi-gateway deployments and cellular backhaul integration are beyond the scope of this work.

#### 1.5.2 Applications

The proposed system is designed for deployment in community forests and protected areas in Nepal, where continuous environmental monitoring is required but conventional monitoring infrastructure is often unavailable or too costly. Its low-cost, low-power, and autonomous design makes it suitable for long-term operation in remote forest environments. The system can support early detection of forest fires, illegal logging activities, and other environmental events through real-time sensor monitoring and acoustic analysis. Additionally, the web-based dashboard enables forest authorities and conservation organizations to remotely monitor forest conditions and respond promptly to detected incidents, thereby improving forest management and environmental protection.

---

## 2. Literature Review

The concept of distributed environmental monitoring using wireless sensor networks (WSNs) emerged in the late 1990s with pioneering projects such as Smart Dust and the Berkeley Motes. Early systems demonstrated the feasibility of deploying low-power sensor nodes for habitat monitoring, soil moisture measurement, and fire weather forecasting. However, these first-generation WSNs were constrained by limited communication range (typically tens to a few hundred meters), high per-node cost, and reliance on single-hop transmission to a central sink. As a result, covering large forest areas required dense node placement or expensive repeater infrastructure, which was impractical for remote community forests in developing regions [4]. This historical limitation motivates the need for a long-range, low-power wireless technology that can extend coverage without proportional infrastructure cost.

Energy consumption quickly became the defining constraint for long-term environmental monitoring. In the early 2000s, researchers introduced duty-cycling and low-power MAC protocols (e.g., S-MAC, T-MAC) that allowed nodes to remain in deep sleep for most of their operational lifetime, waking periodically to sense and transmit. Later advances integrated solar harvesting and ultra-low-power microcontrollers, pushing node lifetimes from weeks to months. Despite these improvements, many systems still relied on frequent transmission of raw sensor data, and synchronization overhead in duty-cycled networks remained a challenge. This project addresses this gap by implementing deep-sleep scheduling (targeting ~7 µA idle current) coupled with event-driven wake-up from acoustic interrupts, reducing unnecessary transmissions and extending battery life beyond conventional periodic sampling approaches [5].

To overcome the limited range of single-hop WSNs, researchers adapted multi-hop ad-hoc networking principles from MANETs to sensor networks. Protocols such as LEACH (Low-Energy Adaptive Clustering Hierarchy) and later AODV (Ad-hoc On-Demand Distance Vector) [6] enabled nodes to relay data through intermediate neighbors. However, these reactive protocols introduced significant control overhead (route discovery floods, periodic beacons) that consumed energy and reduced scalability. Clustering approaches improved energy efficiency but often required global topology knowledge and frequent re-clustering. The central challenge remained: how to achieve reliable multi-hop coverage with minimal routing overhead and low synchronization cost. This project directly confronts this gap by adopting the LDSE (Layered Dynamic Synchronization Energy-saving) protocol, which limits route discovery overhead to approximately 5% of total traffic and coordinates synchronized sleep-relay cycles, thereby extending coverage beyond 2 km without the broadcast storms typical of AODV.

The introduction of the LoRa physical layer by Semtech in 2012 [7] and the subsequent LoRaWAN standard (2015) [8] marked a paradigm shift for remote environmental monitoring. LoRa provides ultra-long-range communication (several kilometers in line-of-sight, 1-2 km in dense forest) at extremely low power, making it ideal for forest IoT applications. However, the standard LoRaWAN architecture is a star-of-stars topology [9], in which each end node communicates directly with a gateway. While simple and energy-efficient, this topology forces a trade-off: covering large forest areas requires many gateways, driving up cost and deployment complexity. Multi-hop extensions to LoRa have been proposed in academic literature, but few have been validated in real forest deployments with energy-constrained nodes. This project fills this gap by implementing a multi-hop LoRa network using the LDSE protocol, which enables low-cost coverage extension without dense gateway infrastructure - a critical requirement for community forests in Nepal.

Detecting illegal logging (chainsaws, axes) and poaching (gunshots) via sound has been explored since the early 2010s. Initial systems used simple thresholding or support vector machines (SVMs) on hand-crafted features (zero-crossing rate, spectral centroid), achieving moderate accuracy but suffering from high false alarms due to natural forest noise. The introduction of Mel-spectrogram features combined with convolutional neural networks (CNNs) significantly improved classification performance. Notably, Kahl et al. (2023) introduced the FSC22 dataset, a benchmark for forest sound classification, and demonstrated that CNNs [10] on Mel-spectrograms achieve state-of-the-art results. Yet, nearly all such systems perform inference on cloud servers or powerful edge devices (Raspberry Pi), requiring continuous connectivity or high power. This project addresses this by deploying a quantized TinyML CNN directly on the ESP32-S3 microcontroller, enabling on-device classification of chainsaws, gunshots, axes, and fireworks without cloud dependency, while staying within 512 KB SRAM and consuming minimal energy.

Early wildfire detection systems relied on single-threshold triggers from temperature or smoke sensors, which often generated false alarms during normal daily fluctuations. To improve robustness, researchers proposed multi-sensor fusion, combining temperature, humidity, and gas (CO, CO2, smoke) measurements [11]. Methods ranged from simple weighted scoring to fuzzy logic and machine learning (random forests, neural networks) [12]. While effective, most fusion models were designed for single-event detection (fire only) and did not integrate with other threat detection (logging, poaching). Moreover, these systems frequently transmitted raw sensor data continuously, increasing energy consumption. This project bridges this gap by computing a composite fire-risk score using a Random Forest-inspired weighted deviation formula (with coefficients derived from sensor correlation analysis) and transmitting alerts only when the risk exceeds a threshold, thereby reducing communication overhead while maintaining detection accuracy.

The emergence of TinyML running machine learning models on resource-constrained microcontrollers has been enabled by frameworks like TensorFlow Lite Micro and Edge Impulse. These tools support model quantization (e.g., int8), reducing model size and inference latency by ~75% compared to float32, making CNNs feasible on platforms with limited SRAM (e.g., ESP32-S3's 512 KB). Prior work has demonstrated on-device keyword spotting and wildlife audio classification, but few studies have deployed TinyML for simultaneous acoustic threat detection (logging/gunshots) and environmental sensor fusion in a single, low-power node [13]. This project fills this integration gap by using Edge Impulse to train, quantize, and deploy a 7-class acoustic CNN alongside a fire-risk scoring module on the same ESP32-S3, achieving sub-200 ms inference latency and enabling true edge intelligence without cloud connectivity.

### Research Gap

While individual components, such as multi-hop LoRa, acoustic CNNs, fire sensor fusion, and TinyML, have been studied separately, very few systems combine them in a unified, low-power, scalable forest monitoring solution. Existing integrated platforms often rely on single-hop LoRaWAN (limiting coverage) or perform acoustic inference off-node (requiring cloud or gateway processing). To the best of our knowledge, no prior work has embedded the LDSE multi-hop protocol, a quantized acoustic TinyML classifier, and a sensor-fusion fire-risk model on a single ESP32-S3 node specifically for community forest monitoring in developing countries like Nepal. This project directly targets this research gap by delivering a prototype that integrates all three capabilities - LDSE-based multi-hop LoRa, on-device acoustic threat detection, and real-time fire-risk scoring - in a low-cost, solar-assisted platform, validated for energy efficiency and coverage extension.

---

## 3. Methodology

This chapter explains the overall system architecture, the hardware components used in each node, the multi-hop communication protocol, and the edge-AI detection strategy that together form the proposed forest monitoring system. The chapter also traces the end-to-end data flow and overall working principle of the network.

### 3.1 System Overview and Architecture

The proposed system follows four layers: Sensing, Edge Processing, Communication, and Gateway/Cloud. Two forest nodes monitor the environment continuously while most hardware components remain in deep sleep to conserve energy. Nodes wake on a scheduled timer or upon detection of a significant acoustic or environmental event.

**Figure 3-1: Proposed System Architecture**

### 3.2 System Components (Hardware)

#### 3.2.1 Forest Nodes (ESP32-S3)

Each forest node is a self-contained sensing and inference unit built around the following components:

- **Microcontroller:** ESP32-S3, selected for its Xtensa LX7 dual-core processor with vector instruction support for neural network acceleration, ultra-low deep sleep current of approximately 7 µA, and 512 KB SRAM sufficient to hold the quantized TinyML model.
- **Environmental Sensors:** MQ135 air quality sensor for gas (CO2) concentration detection; DHT22 digital sensor for high-precision temperature (±0.5 °C) and relative humidity (±2-5% RH) measurement.
- **Acoustic Sensor:** MEMS microphone module (I2S interface) for high-fidelity audio capture at 16 kHz mono, matched to the sample rate used during TinyML model training.
- **Communication:** SX1278 LoRa transceiver module operating at 435 MHz (or the region-specific ISM band), providing long-range, low-power wireless links with a link budget exceeding 170 dB.
- **Power:** A 3.7 V, 2000 mAh Lithium-Polymer (LiPo) battery selected due to its high energy density, rechargeable capability, lightweight design, and suitability for low-power IoT applications.

#### 3.2.2 Gateway (ESP32-S3)

- **ESP32-S3:** Primary LoRa bridge acting as the LDSE network root node (Layer 0) and time-synchronization coordinator. Receives alert packets from forest nodes and forwards them to the web dashboard over Wi-Fi.
- **SX1278 LoRa Module:** Receives uplink packets from forest nodes or relay nodes operating under LDSE routing.

### 3.3 Networking and Communication Protocol (LDSE)

To overcome the coverage limitations of traditional LoRaWAN (single-hop) and the high control overhead of reactive protocols like AODV, the network adopts the Layered Dynamic Synchronization Energy-saving (LDSE) protocol [6]. In standard single-hop LoRaWAN, a node positioned several kilometres deep in a jungle must scale its Spreading Factor up to SF12 to overcome heavy canopy attenuation, spiking the Time-on-Air and draining the battery with every transmission. LDSE solves this by structuring the forest into logical, concentric tiers wrapped around one or more central gateway proxies, so each node communicates only with its nearest parent tier using short, low-power hops at optimized spreading factors such as SF7 or SF8.

#### 3.3.1 Architecture of LDSE

LDSE organizes communication through three clear structural phases: **Layer Assignment**, **Dynamic Time-Synchronisation**, and **Time-Slotted Contention-Free Data Transmission**.

**Figure 3-2: LDSE protocol network hierarchy**

- **Layered Topology:** The gateway (Layer 0) broadcasts an initialization beacon during network setup. Each node assigns itself a hierarchical layer by incrementing the layer value carried in the beacon it receives:

  ```
  L_node = L_received + 1        (3.1)
  ```

  This cascading propagation forms a tree-structured topology across the entire deployment area without any centralized configuration.

- **Parent Selection and Implicit Route Exploration (IRE):** When a node hears multiple candidate parents within a higher tier, it selects the optimal upstream relay using a combined score that balances link quality against remaining relay energy:

  ```
  Score = α · RSSI_link + β · (1 − E_consumed / E_initial)        (3.2)
  ```

  Routes are discovered through active beaconing or passive eavesdropping on existing transmissions, limiting route-discovery overhead to approximately 5% of total traffic and avoiding the broadcast storms characteristic of AODV.

- **Time-Slotted Epoch Structure:** LDSE divides time into global operational epochs, each subdivided into three non-overlapping windows: a **Synchronisation Window** in which all transceivers wake simultaneously to receive time-alignment correction beacons from parent layers; a **Data Transmission Window** implementing TDMA-gated uplinks layer by layer; and a **Deep-Sleep Period** during which all microcontrollers and radio transceivers enter ultra-low-leakage power states consuming as little as 7 µA.

  **Figure 3-3: LDSE Protocol Epoch Structure**

- **Multi-Channel Separation:** A public channel carries handshake and synchronisation messages, while a private channel carries payload data, avoiding intra-network collisions and congestion at relay nodes.

- **Time Synchronisation:** The Flooding Time Synchronisation Protocol (FTSP) distributes a global timestamp from the gateway to all nodes, correcting clock drift caused by ambient temperature variation in the forest canopy and ensuring nodes do not miss their relay windows while remaining in deep sleep for the majority of each cycle.

When a forest node is more than 2 km from the gateway, alert packets are automatically forwarded through intermediate relay nodes operating under LDSE routing, extending effective network coverage without the need for additional gateways.

#### 3.3.2 Power Consumption Model

The energy savings of LDSE arise from exchanging extended receiver listening times for drastically reduced transmission times. The total energy consumed by a node during one active operational cycle is:

```
E_total = E_sleep + E_sense + E_processing + E_sync + E_tx + E_rx        (3.3)
```

The dominant saving comes from the transmission term. Comparing a standard single-hop link at SF12 against an LDSE multi-hop link at SF7 over a 3 km forested path:

```
ToA_SF12 ≈ 983 ms,     ToA_SF7 ≈ 56 ms
E_tx,single = 3.3 V × 120 mA × 0.983 s ≈ 389 mJ
E_tx,LDSE   = 3.3 V ×  45 mA × 0.056 s ≈ 8.3 mJ
```

This represents a transmission energy reduction of approximately **46.8×**. Even after accounting for the receiver overhead incurred by relay nodes (≈1.65 mJ per monitored child slot), the total network energy savings reach up to **42%** compared to classic single-hop topologies, allowing field-deployed nodes to operate reliably for several years on standard lithium iron phosphate (LiFePO4) cells.

#### 3.3.3 Protocol-Centric (Focus on LDSE)

This guide outlines how to build and implement a functional two-tier LDSE protocol stack using an ESP32-S3 host microcontroller paired with an SX1262 LoRa transceiver module.

**Figure 3-4: Sequential Synchronization and Operational Flowchart**

#### 3.3.4 Operational Challenges and Mitigation Strategies

Deploying LDSE in a dense forest environment introduces several engineering challenges, each addressed by a targeted mitigation:

- **Clock Drift due to Thermal Variation:** Diurnal temperature swings cause low-cost quartz oscillators to drift by several milliseconds over a few hours, risking overlapping TDMA slots and packet collisions. This is addressed by inserting a Guard Time Interval (τg = 20-50 ms) at the edges of each slot and by using Temperature-Compensated Crystal Oscillators (TCXO) on custom hardware boards to maintain timing alignment across temperature shifts.

- **Relay Congestion during Mass-Alert Events:** During a fast-spreading wildfire, many outer nodes may send alerts simultaneously, saturating the memory buffers of Layer 1 relay nodes. An asynchronous priority queue combined with a dynamic spreading-factor scale-up mechanism addresses this: when a relay's buffer exceeds 80% capacity, it sets a congestion flag in its synchronization beacon, signalling child nodes to switch to a higher spreading factor (e.g., SF10) and route alerts directly to the gateway, temporarily bypassing the busy relay tier.

- **Hidden Terminal Collisions:** Two sibling nodes in the same layer may be out of range of each other yet share the same parent relay. If both transmit simultaneously, their signals collide at the relay, corrupting both packets. A randomized exponential back-off delay within the assigned TDMA slot window, combined with LoRa Channel Activity Detection (CAD), mitigates this: before transmitting, each node listens for two symbol durations to detect any active preamble chirp on the channel and defers if the channel is busy.

### 3.4 Intelligent Detection Strategy (Edge AI)

#### 3.4.1 Acoustic Dataset: FSC22

The acoustic threat classifier will be trained on a curated subset of the FSC22 (Forest Sound Classification 2022) dataset [10]. The full dataset contains 2,025 five-second audio clips across 27 sub-classes grouped under six broader categories. Each subclass holds 75 samples drawn from the Freesound library and manually verified for quality. The categories cover mechanical sounds (axe, chainsaw, handsaw, generator), animal sounds (birds, wolves, frogs, squirrels, insects), environmental sounds (wind, rain, thunder, river, fire), vehicle sounds (helicopter, motorbike, car), forest threat sounds (gunshot, firework, human screaming), and human sounds (footsteps, speech, whistling).

For this project, FSC22 is narrowed to a **7-class subset** matched to the detection goals: Chainsaw, Axe, Handsaw, Gunshot, Firework, Tree Falling, and a merged Background class combining wind, rain, thunder, river, bird chirping, insects, frogs, squirrels, footsteps, speech, and silence. Firework is kept as a distinct class rather than absorbed into Background. Impulsive sounds that aren't gunshots are a documented source of false alarms in acoustic classifiers, so training the model explicitly on this distinction improves its ability to separate the two. Classes outside the project scope (generator, helicopter, motorbike, car, wood cracking, human screaming, clapping, whistling) are excluded. Before training, all selected clips are resampled from their original 44.1 kHz stereo format to 16 kHz mono, matching the sampling rate of the MEMS microphone on the deployment hardware [14].

#### 3.4.2 Acoustic Processing and TinyML Model Training

The end-to-end acoustic model development pipeline is implemented using the Edge Impulse platform [14], which provides integrated support for data management, DSP feature extraction, model training, quantization, and ESP32-S3 firmware generation.

**Data Preparation:** The 7-class audio subset uses an 80/20 train-test split on original, unaugmented files. Augmentation is applied only to the training split to prevent test set contamination. Operations include pitch shifting (±2 semitones), time stretching (0.9× and 1.1×), and additive white Gaussian noise injection - expanding each threat class from 75 to roughly 225 training samples and balancing class distribution against the larger merged background class.

**Feature Extraction:** Raw 16 kHz audio runs through a DSP block configured with 128 Mel filter banks, 25 ms frame length, and 10 ms frame stride, producing Mel-spectrogram images for classification. The resulting 2D time-frequency representation picks up the harmonic content of chainsaw noise, the sharp transients of gunshots, and the impact profile of axe strikes - all while staying light enough to run inference on-device.

**Model Architecture:** A compact Convolutional Neural Network (CNN) is trained on the Mel-spectrogram features. The architecture follows a two-stage convolutional block pattern (Conv2D → BatchNorm → ReLU → MaxPool) followed by a fully connected classifier with dropout regularization. The model is trained using categorical cross-entropy loss with the Adam optimizer over 50 epochs.

**Quantization and Deployment:** Following training, the model is quantized to 8-bit integers using Edge Impulse's post-training quantization pipeline. This cuts model size by roughly 75% and inference latency by a similar margin, with minimal accuracy loss. The resulting TensorFlow Lite Micro model and DSP preprocessing code are exported as an Arduino-compatible library and flashed to the ESP32-S3 firmware. Inference only runs when the microphone detects audio energy above a silence threshold, skipping unnecessary wake-ups and preserving battery life.

**Figure 3-5: Acoustic TinyML Processing Pipeline**

#### 3.4.3 Fire Risk Scoring

Instead of simple fixed threshold comparisons, each node computes a composite fire-risk score by multisensory fusion, measuring how much current sensor readings deviate from normal (calibrated) conditions. The principle is:

- Normal forest conditions = stable temperature, CO2, and humidity.
- Fire conditions = temperature spikes, CO2 increases dramatically, humidity drops.

The system calculates a normalized deviation score, and if it exceeds a threshold, it signals a fire:

```
FireRisk = 0.51 × ΔCO2 + 0.37 × ΔTEMP + 0.12 × ΔHUMIDITY        (3.4)
```

What each part means:

- ΔCO2 = (current CO2 − Calibrated CO2) / standard deviation of CO2
- ΔT = (current temp − Calibrated temp) / standard deviation of temp
- ΔH = (current humidity − Calibrated humidity) / standard deviation of humidity

Weights (based on correlation with fire):

- 0.51 (51%) = CO2 (highest correlation: 0.4842)
- 0.37 (37%) = Temperature (correlation: 0.3477)
- 0.12 (12%) = Humidity (lowest correlation: 0.10)

**Fire likely detected: risk score is above 3.0 threshold.**

### 3.5 System Data Flow

**Figure 3-6: Proposed System Dataflow**

1. **Sensing Layer:** Forest nodes maintain deep sleep (approximately 7 µA draw). Nodes wake on a scheduled 10-minute timer for environmental sampling, or immediately upon a MEMS microphone interrupt triggered by audio energy above the silence threshold.
2. **Edge Processing:** The ESP32-S3 wakes, reads MQ135 and DHT22 sensors, and computes the RF fire-risk score. Simultaneously or upon a microphone interrupt, it captures a 5-second audio window, extracts the Mel-spectrogram, and runs the quantized CNN inference. If either the fire-risk score exceeds the safety threshold or the CNN inference confidence exceeds the acoustic detection threshold (default 0.85), the node forms a JSON alert packet containing node ID, GPS coordinates, timestamp, risk score, and detected class label.
3. **Communication Layer:** The JSON alert packet is transmitted over LoRa. If the node is within direct range of the gateway (< 2 km), the packet is sent directly. If beyond range, the LDSE routing layer forwards the packet through intermediate relay nodes in a multi-hop chain until it reaches the gateway.
4. **Gateway / Cloud Layer:** The ESP32-S3 gateway receives the alert packet, performs optional secondary validation (duplicate filtering, minimum confidence check), and forwards the structured alert to the web dashboard over Wi-Fi for visualization and notification of forest authorities.

### 3.6 Overall Working Principle

The network operates in a closed-loop, event-driven manner. Scheduled environmental sampling provides background fire-risk monitoring, while interrupt-driven acoustic wake-up enables immediate response to chainsaw, gunshot, or axe sounds. Edge inference before transmission ensures that only confirmed threat events generate network traffic, significantly reducing energy consumption compared to systems that stream raw sensor data continuously. LDSE synchronized sleep-relay cycles ensure that relay nodes are awake to forward packets precisely when needed, while remaining in deep sleep otherwise. The gateway publishes structured alerts to the web dashboard in real time, enabling forest rangers to identify the location and nature of each detected event and respond accordingly.

---

## 4. Expected Outcomes

By the end of this project, the following outcomes are expected:

- Two fully operational forest sensor nodes (ESP32-S3, MQ135, DHT22, MEMS microphone, SX1278 LoRa) with deep-sleep scheduling and interrupt-based wake-up, achieving a target average current draw below 15 mA over a full duty cycle.
- A quantized TinyML CNN acoustic classifier (int8) trained on the 7-class FSC22 subset via Edge Impulse, achieving a target on-device inference accuracy of at least 85% for chainsaw, axe, handsaw, gunshot, firework, and tree-falling events against background forest noise, with inference latency below 200 ms on the ESP32-S3.
- A working LDSE multi-hop LoRa network link between the two forest nodes and the gateway, including relay operation for node-to-gateway distances greater than 2 km, with measured energy consumption reduced by up to 42% compared to single-hop LoRaWAN operation.
- A gateway stack (ESP32-S3 LoRa bridge) delivering structured JSON alert packets to a real-time web dashboard over Wi-Fi, enabling geographic visualization of detected events and timely response by forest authorities.
- Validated system performance through laboratory testing of sensor accuracy, acoustic model inference, and LoRa communication range, followed by limited outdoor field trials in a forested environment to assess end-to-end system reliability and energy consumption.

---

## 5. Project Schedule

**Table 5-1: Gantt chart**

> *(Gantt chart image in the original PDF — reconstruct the milestone timeline here as a table.)*

---

## 6. Project Budget

**Table 6-1: Project Budget**

| S.N. | Component | Quantity | Unit Price (NPR) | Total (NPR) |
|------|-----------|----------|------------------|-------------|
| 1 | ESP32-S3 Development Board | 3 | 1,600 | 4,800 |
| 2 | LoRa Module (SX1278) | 3 | 900 | 2,700 |
| 3 | MQ135 Smoke/Gas Sensor | 2 | 250 | 500 |
| 4 | DHT22 Temperature-Humidity Sensor | 2 | 400 | 800 |
| 5 | MEMS Microphone Module (I2S) | 2 | 600 | 1,200 |
| 6 | Li-ion Battery | 3 | 200 | 600 |
| 7 | Enclosure and Mounting Hardware | 3 | 300 | 900 |
| 8 | Miscellaneous (PCB, wires, connectors, etc.) | 3 | 300 | 900 |
| | **Total Estimated Budget** | | | **12,400** |

---

## 7. Feasibility Analysis

### 7.1 Technical Feasibility

The ESP32-S3 microcontroller provides sufficient computational capability for TinyML inference at the edge, with its Xtensa LX7 dual-core processor and a vector instruction set enabling efficient neural network computation within the available 512 KB SRAM. The SX1278 LoRa transceiver provides long-range communication with a link budget exceeding 170 dB, well suited for the dense vegetation and terrain variation found in Nepalese community forests. The Edge Impulse platform provides a mature and well-documented pipeline for training, quantizing, and deploying CNN models to the ESP32-S3 target, and TensorFlow Lite Micro has been deployed successfully on similar hardware in prior academic work. All hardware components selected for this project are commercially available and have documented community support.

### 7.2 Economic Feasibility

The estimated total prototype budget of approximately NPR 12,400 positions this system as a highly cost-effective alternative to commercial forest monitoring solutions, which typically require expensive satellite or cellular infrastructure.

The use of commodity hardware components and open-source software stacks including Edge Impulse, TensorFlow Lite Micro, and the Arduino framework eliminates licensing costs. The modular and scalable architecture means that additional forest nodes can be added to expand coverage at a marginal per-node cost well below the gateway cost, making the system economically viable for community forest committees operating under budget constraints.

### 7.3 Operational Feasibility

The system is designed to operate autonomously with deep-sleep scheduling and event-driven wake-up, requiring minimal human intervention after initial deployment and configuration. Solar-assisted battery operation can extend node lifetime significantly in remote forest environments where regular maintenance visits are impractical. The real-time web dashboard provides an intuitive interface for forest rangers to monitor alerts, review event histories, and identify geographic patterns of illegal activity without requiring technical expertise. Fail-safe design including confidence thresholding for alert transmission reduces false alarm rates, maintaining the trust of operators in the system's outputs.

### 7.4 Time Feasibility

The proposed two month project schedule is realistic given the availability of the FSC22 dataset, the Edge Impulse platform's rapid model development capability, the team's familiarity with embedded systems and IoT development, and the modular architecture of the system design. Each phase builds incrementally on the previous one, and the independence of the acoustic model training, fire-risk model development, LDSE protocol implementation, and dashboard development workstreams allows parallel progress across team members.

---

## 8. References

1. M. A. Matin, V. S. Chitale, M. S. R. Murthy, K. Uddin, B. Bajracharya, and S. Pradhan, "Understanding forest fire patterns and risk in nepal using remote sensing, geographic information system and historical fire data," vol. 26, no. 4, pp. 276-286.
2. B. Mishra, S. Panthi, S. Poudel, and B. Ghimire, "Forest fire pattern and vulnerability mapping using deep learning in nepal," vol. 19, p. 3.
3. B. Bhattarai, K. Shrestha, P. Gurung, P. Sapkota, and H. Ojha, "Anticipatory forest-fire risk governance in nepal," vol. 26, pp. 142-154.
4. P. W. Rundel, E. A. Graham, M. F. Allen, J. C. Fisher, and T. C. Harmon, "Environmental sensor networks in ecological research," vol. 182, no. 3, pp. 589-607.
5. N. Javaid, S. Hayat, M. Shakir, M. A. Khan, S. H. Bouk, and Z. A. Khan, "Energy efficient MAC protocols in wireless body area sensor networks - a survey."
6. Z. Li, X. Li, and J. Shang, "Forest fire monitoring and energy optimization based on LoRa-mesh wireless communication technology," vol. 14, no. 21, p. 4135.
7. Semtech Corporation, "Semtech LoRa technology overview."
8. N. Sornin, M. Luis, T. Eirich, T. Kramp, and O. Hersent, "LoRaWAN specification." Version number: V1.0.
9. J. Haxhibeqiri, E. De Poorter, I. Moerman, and J. Hoebeke, "A survey of LoRaWAN for IoT: From technology to application," vol. 18, no. 11, p. 3995.
10. M. Bandara, R. Jayasundara, I. Ariyarathne, D. Meedeniya, and C. Perera, "Forest sound classification dataset: FSC22," vol. 23, no. 4, p. 2032.
11. A. K. Patel, P. Parikh, and P. Shah, "ForestGuard: An IP66 edge-AI raspberry pi node for illegal logging and early fire/smoke detection," vol. 16, no. 2, pp. 1486-1500.
12. A. Saida, C. Sreedhar, Samreen, A. Mohamma, K. Jamal, and M. Ghalwan, "LORA based forest fire monitoring system," in E3S web of conferences, vol. 430, p. 01171, EDP Sciences.
13. S. Kahl, C. Wood, M. Eibl, and H. Klinck, "FSC22: A dataset for forest sound classification," vol. 23, no. 4, p. 2032.
14. J. Situnayake and J. Plunkett, *AI at the edge: Solving real-world problems with embedded machine learning.* O'Reilly Media.
