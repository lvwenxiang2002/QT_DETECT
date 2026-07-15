#ifndef INFERTHREAD_H
#define INFERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QLibrary>
#include <memory>
#include <vector>

#include "../InferLib/InferAPI.h"
#include "../Common.h"

class InferThread : public QThread
{
    Q_OBJECT
public:
    explicit InferThread(QObject *parent = nullptr);
    ~InferThread();

    void stop();
    bool initializeTRT(const QString& engineBacklightPath, const QString& engineDarkfieldPath);
    
public slots:
    void pushImage(std::shared_ptr<ImageFrame> frame);

signals:
    void inferenceCompleted(std::shared_ptr<ImageFrame> frame);
    void errorOccurred(QString errorMsg);

protected:
    void run() override;

private:
    void processFrame(std::shared_ptr<ImageFrame> frame);

    bool m_isRunning;
    QMutex m_mutex;
    QWaitCondition m_condition;
    QQueue<std::shared_ptr<ImageFrame>> m_queue;

    QLibrary m_inferLib;
    typedef IInferenceEngine* (*CreateEngineFunc)();
    typedef void (*DestroyEngineFunc)(IInferenceEngine*);
    CreateEngineFunc m_createFunc = nullptr;
    DestroyEngineFunc m_destroyFunc = nullptr;

    IInferenceEngine* m_engine = nullptr;
};

#endif // INFERTHREAD_H
