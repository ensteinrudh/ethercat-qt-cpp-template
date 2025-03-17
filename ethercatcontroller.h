// ethercatcontroller.h
#ifndef ETHERCATCONTROLLER_H
#define ETHERCATCONTROLLER_H

#include <QMutex>
#include <QObject>
#include <QString>
#include <atomic>
#include <ecrt.h>
#include <thread>

class EtherCatController : public QObject {
  Q_OBJECT
  Q_PROPERTY(
      int actualPosition READ actualPosition NOTIFY actualPositionChanged)
  Q_PROPERTY(QString statusWord READ statusWord NOTIFY statusWordChanged)
  Q_PROPERTY(
      QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
  Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
  Q_PROPERTY(
      bool readyForCommand READ isReadyForCommand NOTIFY readyForCommandChanged)

public:
  explicit EtherCatController(QObject *parent = nullptr);
  ~EtherCatController();

  int actualPosition() const { return m_actualPosition; }
  QString statusWord() const { return m_statusWord; }
  QString statusMessage() const { return m_statusMessage; }
  bool isConnected() const { return m_connected; }

public slots:
  bool initialize();
  void moveToPosition(int position, int velocity);
  void shutdown();

signals:
  void actualPositionChanged(int position);
  void statusWordChanged(QString statusWord);
  void statusMessageChanged(QString status);
  void connectedChanged(bool connected);
  void readyForCommandChanged(bool ready);

private:
  // EtherCAT components
  ec_master_t *master;
  ec_domain_t *domain;
  ec_slave_config_t *slave_config;
  uint8_t *domain_pd;

  // PDO offset variables
  unsigned int ctrl_word_offset;
  unsigned int status_word_offset;
  unsigned int target_pos_offset;
  unsigned int target_vel_offset;
  unsigned int op_mode_offset;
  unsigned int op_mode_display_offset;
  unsigned int actual_pos_offset;
  unsigned int error_code_offset;

  // Thread safety and real-time components
  std::thread rt_thread;
  std::atomic<bool> running;
  QMutex data_mutex;

  // State tracking
  int m_actualPosition;
  QString m_statusWord;
  QString m_statusMessage;
  bool m_connected;
  int m_cycleCount;
  std::atomic<bool> m_commandPending;
  std::atomic<bool> m_motionInProgress;
  int m_targetPosition;
  int m_targetVelocity;

  // Helper methods
  void setStatusMessage(const QString &msg);
  void cleanup();
  int configPDOs();
  bool isReadyForCommand() const { return m_connected && !m_motionInProgress; }

  // Real-time thread methods
  void rtThreadFunc();
  void cyclicTask();
  void stack_prefault();

  // Thread-safe property updates
  void updateActualPosition(int position);
  void updateStatusWord(const QString &statusWord);
};

#endif // ETHERCATCONTROLLER_H
