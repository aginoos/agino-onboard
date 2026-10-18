// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QFont>

// The expanded candidate layout, materialized only as far as the viewport.
// Roles and entry maps deliberately match CandidateRows so the renderer can
// consume either model without changing candidate identity or geometry.
class NativeCandidateRows final : public QAbstractListModel {
 Q_OBJECT
 Q_PROPERTY(QStringList candidates READ candidates WRITE setCandidates NOTIFY layoutChanged)
 Q_PROPERTY(qreal availableWidth READ availableWidth WRITE setAvailableWidth NOTIFY layoutChanged)
 Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY layoutChanged)
public:
 enum { Entries=Qt::UserRole+1, RowHeight };
 explicit NativeCandidateRows(QObject *parent=nullptr):QAbstractListModel(parent){}
 int rowCount(const QModelIndex &parent={})const override;
 QVariant data(const QModelIndex&,int role)const override;
 QHash<int,QByteArray> roleNames()const override;
 QStringList candidates()const{return candidates_;}
 qreal availableWidth()const{return width_;}
 QFont font()const{return font_;}
 void setCandidates(const QStringList&);
 void setAvailableWidth(qreal);
 void setFont(const QFont&);
 // bottom is in content coordinates, including the 8px inter-row gaps. Keep
 // the row containing that coordinate plus one following row ready to paint.
 void ensureLayout(qreal bottom);
 // Includes inter-row gaps, with no trailing gap. Unseen rows are estimated
 // from the measured prefix; this becomes exact when complete() is true.
 qreal contentHeight()const;
 bool complete()const{return next_==candidates_.size();}
 // Counts shaping work, including the single cached lookahead candidate.
 // Useful for verifying that an unseen script does not open fallback fonts.
 int measuredCandidateCount()const{return next_+(pending_?1:0);}
signals:
 void layoutChanged();
private:
 struct Row {QVariantList entries;qreal height=48,bottom=0;};
 struct Entry {QString text;int index=0;qreal width=0,height=48;};
 void invalidate();
 void appendRow();
 Entry measure(int)const;
 QVector<Row> rows_;
 QStringList candidates_;
 QFont font_;
 qreal width_=800,extent_=0;
 int next_=0;
 bool pending_=false;
 Entry lookahead_;
};
