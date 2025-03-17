// ethercatcontroller.cpp
#include "ethercatcontroller.h"
#include <QDebug>
#include <QMetaObject>
#include <errno.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#define VENDOR_ID 0x00004321
#define PRODUCT_CODE 0x000010ba
#define ALIAS 0
#define POSITION 0
#define PERIOD_NS 4000000 // 4ms cycle time
#define NSEC_PER_SEC 1000000000ULL

EtherCatController::EtherCatController(QObject *parent)
    : QObject(parent), master(nullptr), domain(nullptr), slave_config(nullptr),
      domain_pd(nullptr), running(false), m_actualPosition(0),
      m_statusWord("0x0000"), m_statusMessage("Not initialized"),
      m_connected(false), m_cycleCount(0), m_commandPending(false),
      m_motionInProgress(false), m_targetPosition(0), m_targetVelocity(0) {}

EtherCatController::~EtherCatController() { shutdown(); }

bool EtherCatController::initialize() {
  setStatusMessage("Initializing EtherCAT...");

  // Request master instance
  master = ecrt_request_master(0);
  if (!master) {
    setStatusMessage("Failed to request master");
    return false;
  }

  // Create domain
  domain = ecrt_master_create_domain(master);
  if (!domain) {
    setStatusMessage("Failed to create domain");
    return false;
  }

  // Create slave configuration
  slave_config = ecrt_master_slave_config(master, ALIAS, POSITION, VENDOR_ID,
                                          PRODUCT_CODE);
  if (!slave_config) {
    setStatusMessage("Failed to configure slave");
    return false;
  }

  // Configure PDOs
  if (configPDOs() != 0) {
    setStatusMessage("Failed to configure PDOs");
    return false;
  }

  // Configure PDO entries
  ec_pdo_entry_reg_t domain_entries[] = {
      {ALIAS, POSITION, VENDOR_ID, PRODUCT_CODE, 0x6040, 0, &ctrl_word_offset},
      {ALIAS, POSITION, VENDOR_ID, PRODUCT_CODE, 0x6041, 0,
       &status_word_offset},
      {ALIAS, POSITION, VENDOR_ID, PRODUCT_CODE, 0x607A, 0, &target_pos_offset},
      {ALIAS, POSITION, VENDOR_ID, PRODUCT_CODE, 0x6081, 0, &target_vel_offset},
      {ALIAS, POSITION, VENDOR_ID, PRODUCT_CODE, 0x6060, 0, &op_mode_offset},
      {ALIAS, POSITION, VENDOR_ID, PRODUCT_CODE, 0x6061, 0,
       &op_mode_display_offset},
      {ALIAS, POSITION, VENDOR_ID, PRODUCT_CODE, 0x6064, 0, &actual_pos_offset},
      {ALIAS, POSITION, VENDOR_ID, PRODUCT_CODE, 0x603F, 0, &error_code_offset},
      {}};

  if (ecrt_domain_reg_pdo_entry_list(domain, domain_entries)) {
    setStatusMessage("Failed to register PDO entries");
    return false;
  }

  // Configure DC
  ecrt_slave_config_dc(slave_config, 0x0300, PERIOD_NS, 800000, 0, 0);

  // Activate master
  if (ecrt_master_activate(master)) {
    setStatusMessage("Failed to activate master");
    return false;
  }

  // Get process data
  domain_pd = ecrt_domain_data(domain);
  if (!domain_pd) {
    setStatusMessage("Failed to get domain process data");
    return false;
  }

  // Change status
  setStatusMessage("EtherCAT initialized successfully");
  m_connected = true;
  emit connectedChanged(true);

  // Start real-time thread
  running = true;
  rt_thread = std::thread(&EtherCatController::rtThreadFunc, this);

// Set thread name for debugging
#if defined(__linux__) && defined(_GNU_SOURCE)
  pthread_setname_np(rt_thread.native_handle(), "EtherCAT-RT");
#endif

  return true;
}

void EtherCatController::moveToPosition(int position, int velocity) {
  if (!m_connected) {
    setStatusMessage("EtherCAT not connected");
    return;
  }

  if (m_motionInProgress) {
    setStatusMessage("Motion in progress, wait for target reached before "
                     "sending new command");
    return;
  }

  // Update target values (thread-safe)
  QMutexLocker locker(&data_mutex);
  m_targetPosition = position;
  m_targetVelocity = velocity;
  m_commandPending = true;

  if (m_motionInProgress) {
    setStatusMessage(QString("Command queued: position %1, velocity %2 - will "
                             "execute when current motion completes")
                         .arg(position)
                         .arg(velocity));
  } else {
    setStatusMessage(QString("Moving to position %1 at velocity %2")
                         .arg(position)
                         .arg(velocity));
  }
}

void EtherCatController::stack_prefault() {
  // Pre-fault stack pages to ensure they're in RAM
  const int MAX_STACK_SIZE = 8192;
  unsigned char dummy[MAX_STACK_SIZE];
  memset(dummy, 0, MAX_STACK_SIZE);
}

void EtherCatController::rtThreadFunc() {
  // Set real-time priority
  struct sched_param param;
  param.sched_priority = sched_get_priority_max(SCHED_FIFO);

  qDebug() << "EtherCAT: Using RT priority" << param.sched_priority;
  if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
    qWarning() << "EtherCAT: Failed to set RT scheduler:" << strerror(errno);
  }

  // Lock memory
  if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
    qWarning() << "EtherCAT: Failed to lock memory:" << strerror(errno);
  }

  // Pre-fault stack
  stack_prefault();

  // Setup timing
  struct timespec wakeup_time;
  clock_gettime(CLOCK_MONOTONIC, &wakeup_time);
  wakeup_time.tv_sec += 1; // Start in future
  wakeup_time.tv_nsec = 0;

  // Main RT loop
  while (running) {
    int ret =
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup_time, NULL);
    if (ret) {
      qWarning() << "EtherCAT: clock_nanosleep failed:" << strerror(ret);
      break;
    }

    // Execute cyclic task
    cyclicTask();

    // Calculate next wakeup time
    wakeup_time.tv_nsec += PERIOD_NS;
    while (wakeup_time.tv_nsec >= NSEC_PER_SEC) {
      wakeup_time.tv_nsec -= NSEC_PER_SEC;
      wakeup_time.tv_sec++;
    }
  }

  // Clean up RT settings
  munlockall();
  qDebug() << "EtherCAT: RT thread exiting";
}

void EtherCatController::cyclicTask() {
  // Get current time for DC sync
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  uint64_t time_ns = now.tv_sec * NSEC_PER_SEC + now.tv_nsec;

  // DC sync
  ecrt_master_application_time(master, time_ns);
  ecrt_master_sync_reference_clock(master);
  ecrt_master_sync_slave_clocks(master);

  // Receive process data
  ecrt_master_receive(master);
  ecrt_domain_process(domain);

  // Read status
  uint16_t status = EC_READ_U16(domain_pd + status_word_offset);
  int32_t position = EC_READ_S32(domain_pd + actual_pos_offset);
  bool target_reached = (status & (1 << 10)) != 0;

  // Update motion status when target is reached
  if (target_reached && m_motionInProgress) {
    m_motionInProgress = false;
    emit readyForCommandChanged(true);
    EC_WRITE_U16(domain_pd + ctrl_word_offset, 0x000F);
    // Check if there's a pending command to execute immediately
    if (m_commandPending) {
      QMetaObject::invokeMethod(
          this,
          [this]() {
            setStatusMessage("Target reached - executing pending command");
          },
          Qt::QueuedConnection);
    } else {
      QMetaObject::invokeMethod(
          this,
          [this]() {
            setStatusMessage("Target position reached, ready for new command");
          },
          Qt::QueuedConnection);
    }
  }

  // Thread-safe property updates
  QMetaObject::invokeMethod(
      this, [this, position]() { this->updateActualPosition(position); },
      Qt::QueuedConnection);

  QString statusHex = QString("0x%1").arg(status, 4, 16, QChar('0')).toUpper();
  QMetaObject::invokeMethod(
      this, [this, statusHex]() { this->updateStatusWord(statusHex); },
      Qt::QueuedConnection);

  // State machine for drive enablement
  if ((status & 0x006F) == 0x0040) { // Status: Switch on disabled
    EC_WRITE_U16(domain_pd + ctrl_word_offset, 0x0006);
  } else if ((status & 0x006F) == 0x0021) { // Status: Ready to switch on
    EC_WRITE_U16(domain_pd + ctrl_word_offset, 0x0007);
  } else if ((status & 0x006F) == 0x0023) { // Status: Switched on
    EC_WRITE_U16(domain_pd + ctrl_word_offset, 0x000F);
  } else if ((status & 0x006F) == 0x0027) { // Status: Operation enabled
    // Set operation mode to CSP (Position mode)
    if (m_cycleCount == 10) {
      EC_WRITE_U8(domain_pd + op_mode_offset, 1);
    }

    if (m_commandPending && !m_motionInProgress) {
      int targetPos, targetVel;
      {
        QMutexLocker locker(&data_mutex);
        targetPos = m_targetPosition;
        targetVel = m_targetVelocity;
      }
      // Clear the pending flag
      m_commandPending = false;

      EC_WRITE_S32(domain_pd + target_pos_offset, targetPos);
      EC_WRITE_U32(domain_pd + target_vel_offset, targetVel);

      // Trigger motion
      EC_WRITE_U16(domain_pd + ctrl_word_offset, 0x004F);
      EC_WRITE_U16(domain_pd + ctrl_word_offset, 0x005F);

      // Mark motion as in progress
      m_motionInProgress = true;
    }
  }

  // Send process data
  ecrt_domain_queue(domain);
  ecrt_master_send(master);

  m_cycleCount++;
}

void EtherCatController::updateActualPosition(int position) {
  if (position != m_actualPosition) {
    m_actualPosition = position;
    emit actualPositionChanged(position);
  }
}

void EtherCatController::updateStatusWord(const QString &statusWord) {
  if (statusWord != m_statusWord) {
    m_statusWord = statusWord;
    emit statusWordChanged(statusWord);
  }
}

void EtherCatController::setStatusMessage(const QString &msg) {
  m_statusMessage = msg;
  qDebug() << "EtherCAT:" << msg;
  emit statusMessageChanged(msg);
}

void EtherCatController::shutdown() {
  // Stop RT thread
  if (running) {
    running = false;
    if (rt_thread.joinable()) {
      rt_thread.join();
    }
  }

  cleanup();
}

void EtherCatController::cleanup() {
  if (m_connected && domain_pd) {
    // Safe shutdown sequence - disable drive
    EC_WRITE_U16(domain_pd + ctrl_word_offset, 0x0007);
    ecrt_domain_queue(domain);
    ecrt_master_send(master);
  }

  if (master) {
    ecrt_release_master(master);
    master = nullptr;
  }

  domain = nullptr;
  slave_config = nullptr;
  domain_pd = nullptr;

  m_connected = false;
  emit connectedChanged(false);
  setStatusMessage("EtherCAT disconnected");
}

int EtherCatController::configPDOs() {
  // Define mapping for RxPDO (master to slave)
  ec_pdo_entry_info_t rx_entries[] = {
      {0x6040, 0x00, 16}, // Control Word
      {0x607A, 0x00, 32}, // Target Position
      {0x6081, 0x00, 32}, // Target Velocity
      {0x6060, 0x00, 8},  // Modes of Operation
  };

  // Define mapping for TxPDO (slave to master)
  ec_pdo_entry_info_t tx_entries[] = {
      {0x603F, 0x00, 16}, // Error Code
      {0x6041, 0x00, 16}, // Status Word
      {0x6061, 0x00, 8},  // Modes of Operation Display
      {0x6064, 0x00, 32}, // Position Actual Value
  };

  // Define PDOs
  ec_pdo_info_t rx_pdos[] = {{0x1600, 4, rx_entries}};

  ec_pdo_info_t tx_pdos[] = {{0x1A00, 4, tx_entries}};

  // Define sync managers
  ec_sync_info_t sync_info[] = {{0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
                                {1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},
                                {2, EC_DIR_OUTPUT, 1, rx_pdos, EC_WD_ENABLE},
                                {3, EC_DIR_INPUT, 1, tx_pdos, EC_WD_DISABLE},
                                {0xff}};

  // Configure PDOs
  return ecrt_slave_config_pdos(slave_config, EC_END, sync_info);
}
