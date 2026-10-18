#pragma once
#include <QAbstractListModel>
#include <QFont>
class CandidateRows:public QAbstractListModel {
 Q_OBJECT
 Q_PROPERTY(QStringList candidates READ candidates WRITE setCandidates NOTIFY layoutChanged)
 Q_PROPERTY(qreal availableWidth READ availableWidth WRITE setAvailableWidth NOTIFY layoutChanged)
 Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY layoutChanged)
public:
 enum {Entries=Qt::UserRole+1,RowHeight};
 explicit CandidateRows(QObject *p=nullptr):QAbstractListModel(p){}
 int rowCount(const QModelIndex &p={})const override{return p.isValid()?0:rows_.size();}
 QVariant data(const QModelIndex &i,int role)const override;
 QHash<int,QByteArray> roleNames()const override{return {{Entries,"entries"},{RowHeight,"rowHeight"}};}
 QStringList candidates()const{return candidates_;}qreal availableWidth()const{return width_;}QFont font()const{return font_;}
 void setCandidates(const QStringList &s);void setAvailableWidth(qreal w);void setFont(const QFont &f);
signals:void layoutChanged();
private:
 void rebuild();struct Row {QVariantList entries; qreal height=48;};QVector<Row> rows_;QStringList candidates_;qreal width_=800;QFont font_;
};
