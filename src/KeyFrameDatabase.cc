/*
 * This file is part of PLVS.
 * This file is a modified version present in RGBDSLAM2 (https://github.com/felixendres/rgbdslam_v2)
 * Copyright (C) 2018-present Luigi Freda <luigifreda at gmail dot com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */
/**
* This file is part of ORB-SLAM3
*
* Copyright (C) 2017-2021 Carlos Campos, Richard Elvira, Juan J. Gómez Rodríguez, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
* Copyright (C) 2014-2016 Raúl Mur-Artal, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
*
* ORB-SLAM3 is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM3 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
* the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with ORB-SLAM3.
* If not, see <http://www.gnu.org/licenses/>.
*/


#include "KeyFrameDatabase.h"

#include "KeyFrame.h"
#include "Thirdparty/DBoW2/DBoW2/BowVector.h"
#include "Logger.h"

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include<mutex>

using namespace std;

namespace PLVS2
{

KeyFrameDatabase::KeyFrameDatabase (const ORBVocabulary &voc):
    mpVoc(&voc)
{
    mvInvertedFile.resize(voc.size());
}


void KeyFrameDatabase::add(const KeyFramePtr& pKF)
{
    unique_lock<mutex> lock(mMutex);

    for(DBoW2::BowVector::const_iterator vit= pKF->mBowVec.begin(), vend=pKF->mBowVec.end(); vit!=vend; vit++)
        mvInvertedFile[vit->first].push_back(pKF);
}

void KeyFrameDatabase::erase(const KeyFramePtr& pKF)
{
    unique_lock<mutex> lock(mMutex);

    // Erase elements in the Inverse File for the entry
    for(DBoW2::BowVector::const_iterator vit=pKF->mBowVec.begin(), vend=pKF->mBowVec.end(); vit!=vend; vit++)
    {
        // List of keyframes that share the word
        list<KeyFramePtr> &lKFs =   mvInvertedFile[vit->first];

        for(list<KeyFramePtr>::iterator lit=lKFs.begin(), lend= lKFs.end(); lit!=lend; lit++)
        {
            if(pKF==*lit)
            {
                lKFs.erase(lit);
                break;
            }
        }
    }
}

void KeyFrameDatabase::clear()
{
    mvInvertedFile.clear();
    mvInvertedFile.resize(mpVoc->size());
}

void KeyFrameDatabase::clearMap(Map* pMap)
{
    unique_lock<mutex> lock(mMutex);

    // Erase elements in the Inverse File for the entry
    for(std::vector<list<KeyFramePtr> >::iterator vit=mvInvertedFile.begin(), vend=mvInvertedFile.end(); vit!=vend; vit++)
    {
        // List of keyframes that share the word
        list<KeyFramePtr> &lKFs =  *vit;

        for(list<KeyFramePtr>::iterator lit=lKFs.begin(), lend= lKFs.end(); lit!=lend;)
        {
            KeyFramePtr pKFi = *lit;
            if(pMap == pKFi->GetMap())
            {
                lit = lKFs.erase(lit);
                // Dont delete the KF because the class Map clean all the KF when it is destroyed
            }
            else
            {
                ++lit;
            }
        }
    }
}

vector<KeyFramePtr> KeyFrameDatabase::DetectLoopCandidates(KeyFramePtr& pKF, float minScore)
{
    set<KeyFramePtr> spConnectedKeyFrames = pKF->GetConnectedKeyFrames();
    list<KeyFramePtr> lKFsSharingWords;

    // Search all keyframes that share a word with current keyframes
    // Discard keyframes connected to the query keyframe
    {
        unique_lock<mutex> lock(mMutex);

        for(DBoW2::BowVector::const_iterator vit=pKF->mBowVec.begin(), vend=pKF->mBowVec.end(); vit != vend; vit++)
        {
            list<KeyFramePtr> &lKFs =   mvInvertedFile[vit->first];

            for(list<KeyFramePtr>::iterator lit=lKFs.begin(), lend= lKFs.end(); lit!=lend; lit++)
            {
                KeyFramePtr pKFi=*lit;
                if(pKFi->GetMap()==pKF->GetMap()) // For consider a loop candidate it a candidate it must be in the same map
                {
                    if(pKFi->mnLoopQuery!=pKF->mnId)
                    {
                        pKFi->mnLoopWords=0;
                        if(!spConnectedKeyFrames.count(pKFi))
                        {
                            pKFi->mnLoopQuery=pKF->mnId;
                            lKFsSharingWords.push_back(pKFi);
                        }
                    }
                    pKFi->mnLoopWords++;
                }


            }
        }
    }

    if(lKFsSharingWords.empty())
        return vector<KeyFramePtr>();

    list<pair<float,KeyFramePtr> > lScoreAndMatch;

    // Only compare against those keyframes that share enough words
    int maxCommonWords=0;
    for(list<KeyFramePtr>::iterator lit=lKFsSharingWords.begin(), lend= lKFsSharingWords.end(); lit!=lend; lit++)
    {
        if((*lit)->mnLoopWords>maxCommonWords)
            maxCommonWords=(*lit)->mnLoopWords;
    }

    int minCommonWords = maxCommonWords*0.8f;

    int nscores=0;

    // Compute similarity score. Retain the matches whose score is higher than minScore
    for(list<KeyFramePtr>::iterator lit=lKFsSharingWords.begin(), lend= lKFsSharingWords.end(); lit!=lend; lit++)
    {
        KeyFramePtr pKFi = *lit;

        if(pKFi->mnLoopWords>minCommonWords)
        {
            nscores++;

            float si = mpVoc->score(pKF->mBowVec,pKFi->mBowVec);

            pKFi->mLoopScore = si;
            if(si>=minScore)
                lScoreAndMatch.push_back(make_pair(si,pKFi));
        }
    }

    if(lScoreAndMatch.empty())
        return vector<KeyFramePtr>();

    list<pair<float,KeyFramePtr> > lAccScoreAndMatch;
    float bestAccScore = minScore;

    // Lets now accumulate score by covisibility
    for(list<pair<float,KeyFramePtr> >::iterator it=lScoreAndMatch.begin(), itend=lScoreAndMatch.end(); it!=itend; it++)
    {
        KeyFramePtr pKFi = it->second;
        vector<KeyFramePtr> vpNeighs = pKFi->GetBestCovisibilityKeyFrames(10);

        float bestScore = it->first;
        float accScore = it->first;
        KeyFramePtr pBestKF = pKFi;
        for(vector<KeyFramePtr>::iterator vit=vpNeighs.begin(), vend=vpNeighs.end(); vit!=vend; vit++)
        {
            KeyFramePtr pKF2 = *vit;
            if(pKF2->mnLoopQuery==pKF->mnId && pKF2->mnLoopWords>minCommonWords)
            {
                accScore+=pKF2->mLoopScore;
                if(pKF2->mLoopScore>bestScore)
                {
                    pBestKF=pKF2;
                    bestScore = pKF2->mLoopScore;
                }
            }
        }

        lAccScoreAndMatch.push_back(make_pair(accScore,pBestKF));
        if(accScore>bestAccScore)
            bestAccScore=accScore;
    }

    // Return all those keyframes with a score higher than 0.75*bestScore
    float minScoreToRetain = 0.75f*bestAccScore;

    set<KeyFramePtr> spAlreadyAddedKF;
    vector<KeyFramePtr> vpLoopCandidates;
    vpLoopCandidates.reserve(lAccScoreAndMatch.size());

    for(list<pair<float,KeyFramePtr> >::iterator it=lAccScoreAndMatch.begin(), itend=lAccScoreAndMatch.end(); it!=itend; it++)
    {
        if(it->first>minScoreToRetain)
        {
            KeyFramePtr pKFi = it->second;
            if(!spAlreadyAddedKF.count(pKFi))
            {
                vpLoopCandidates.push_back(pKFi);
                spAlreadyAddedKF.insert(pKFi);
            }
        }
    }


    return vpLoopCandidates;
}

void KeyFrameDatabase::DetectCandidates(KeyFramePtr pKF, float minScore,vector<KeyFramePtr>& vpLoopCand, vector<KeyFramePtr>& vpMergeCand)
{
    set<KeyFramePtr> spConnectedKeyFrames = pKF->GetConnectedKeyFrames();
    list<KeyFramePtr> lKFsSharingWordsLoop,lKFsSharingWordsMerge;

    // Search all keyframes that share a word with current keyframes
    // Discard keyframes connected to the query keyframe
    {
        unique_lock<mutex> lock(mMutex);

        for(DBoW2::BowVector::const_iterator vit=pKF->mBowVec.begin(), vend=pKF->mBowVec.end(); vit != vend; vit++)
        {
            list<KeyFramePtr> &lKFs = mvInvertedFile[vit->first];

            for(list<KeyFramePtr>::iterator lit=lKFs.begin(), lend= lKFs.end(); lit!=lend; lit++)
            {
                KeyFramePtr pKFi=*lit;
                if(pKFi->GetMap()==pKF->GetMap()) // For consider a loop candidate it a candidate it must be in the same map
                {
                    if(pKFi->mnLoopQuery!=pKF->mnId)
                    {
                        pKFi->mnLoopWords=0;
                        if(!spConnectedKeyFrames.count(pKFi))
                        {
                            pKFi->mnLoopQuery=pKF->mnId;
                            lKFsSharingWordsLoop.push_back(pKFi);
                        }
                    }
                    pKFi->mnLoopWords++;
                }
                else if(!pKFi->GetMap()->IsBad())
                {
                    if(pKFi->mnMergeQuery!=pKF->mnId)
                    {
                        pKFi->mnMergeWords=0;
                        if(!spConnectedKeyFrames.count(pKFi))
                        {
                            pKFi->mnMergeQuery=pKF->mnId;
                            lKFsSharingWordsMerge.push_back(pKFi);
                        }
                    }
                    pKFi->mnMergeWords++;
                }
            }
        }
    }

    if(lKFsSharingWordsLoop.empty() && lKFsSharingWordsMerge.empty())
        return;

    if(!lKFsSharingWordsLoop.empty())
    {
        list<pair<float,KeyFramePtr> > lScoreAndMatch;

        // Only compare against those keyframes that share enough words
        int maxCommonWords=0;
        for(list<KeyFramePtr>::iterator lit=lKFsSharingWordsLoop.begin(), lend= lKFsSharingWordsLoop.end(); lit!=lend; lit++)
        {
            if((*lit)->mnLoopWords>maxCommonWords)
                maxCommonWords=(*lit)->mnLoopWords;
        }

        int minCommonWords = maxCommonWords*0.8f;

        int nscores=0;

        // Compute similarity score. Retain the matches whose score is higher than minScore
        for(list<KeyFramePtr>::iterator lit=lKFsSharingWordsLoop.begin(), lend= lKFsSharingWordsLoop.end(); lit!=lend; lit++)
        {
            KeyFramePtr pKFi = *lit;

            if(pKFi->mnLoopWords>minCommonWords)
            {
                nscores++;

                float si = mpVoc->score(pKF->mBowVec,pKFi->mBowVec);

                pKFi->mLoopScore = si;
                if(si>=minScore)
                    lScoreAndMatch.push_back(make_pair(si,pKFi));
            }
        }

        if(!lScoreAndMatch.empty())
        {
            list<pair<float,KeyFramePtr> > lAccScoreAndMatch;
            float bestAccScore = minScore;

            // Lets now accumulate score by covisibility
            for(list<pair<float,KeyFramePtr> >::iterator it=lScoreAndMatch.begin(), itend=lScoreAndMatch.end(); it!=itend; it++)
            {
                KeyFramePtr pKFi = it->second;
                vector<KeyFramePtr> vpNeighs = pKFi->GetBestCovisibilityKeyFrames(10);

                float bestScore = it->first;
                float accScore = it->first;
                KeyFramePtr pBestKF = pKFi;
                for(vector<KeyFramePtr>::iterator vit=vpNeighs.begin(), vend=vpNeighs.end(); vit!=vend; vit++)
                {
                    KeyFramePtr pKF2 = *vit;
                    if(pKF2->mnLoopQuery==pKF->mnId && pKF2->mnLoopWords>minCommonWords)
                    {
                        accScore+=pKF2->mLoopScore;
                        if(pKF2->mLoopScore>bestScore)
                        {
                            pBestKF=pKF2;
                            bestScore = pKF2->mLoopScore;
                        }
                    }
                }

                lAccScoreAndMatch.push_back(make_pair(accScore,pBestKF));
                if(accScore>bestAccScore)
                    bestAccScore=accScore;
            }

            // Return all those keyframes with a score higher than 0.75*bestScore
            float minScoreToRetain = 0.75f*bestAccScore;

            set<KeyFramePtr> spAlreadyAddedKF;
            vpLoopCand.reserve(lAccScoreAndMatch.size());

            for(list<pair<float,KeyFramePtr> >::iterator it=lAccScoreAndMatch.begin(), itend=lAccScoreAndMatch.end(); it!=itend; it++)
            {
                if(it->first>minScoreToRetain)
                {
                    KeyFramePtr pKFi = it->second;
                    if(!spAlreadyAddedKF.count(pKFi))
                    {
                        vpLoopCand.push_back(pKFi);
                        spAlreadyAddedKF.insert(pKFi);
                    }
                }
            }
        }

    }

    if(!lKFsSharingWordsMerge.empty())
    {
        //cout << "BoW candidates: " << lKFsSharingWordsMerge.size() << endl;
        list<pair<float,KeyFramePtr> > lScoreAndMatch;

        // Only compare against those keyframes that share enough words
        int maxCommonWords=0;
        for(list<KeyFramePtr>::iterator lit=lKFsSharingWordsMerge.begin(), lend=lKFsSharingWordsMerge.end(); lit!=lend; lit++)
        {
            if((*lit)->mnMergeWords>maxCommonWords)
                maxCommonWords=(*lit)->mnMergeWords;
        }

        int minCommonWords = maxCommonWords*0.8f;

        int nscores=0;

        // Compute similarity score. Retain the matches whose score is higher than minScore
        for(list<KeyFramePtr>::iterator lit=lKFsSharingWordsMerge.begin(), lend=lKFsSharingWordsMerge.end(); lit!=lend; lit++)
        {
            KeyFramePtr pKFi = *lit;

            if(pKFi->mnMergeWords>minCommonWords)
            {
                nscores++;

                float si = mpVoc->score(pKF->mBowVec,pKFi->mBowVec);

                pKFi->mMergeScore = si;
                if(si>=minScore)
                    lScoreAndMatch.push_back(make_pair(si,pKFi));
            }
        }

        if(!lScoreAndMatch.empty())
        {
            list<pair<float,KeyFramePtr> > lAccScoreAndMatch;
            float bestAccScore = minScore;

            // Lets now accumulate score by covisibility
            for(list<pair<float,KeyFramePtr> >::iterator it=lScoreAndMatch.begin(), itend=lScoreAndMatch.end(); it!=itend; it++)
            {
                KeyFramePtr pKFi = it->second;
                vector<KeyFramePtr> vpNeighs = pKFi->GetBestCovisibilityKeyFrames(10);

                float bestScore = it->first;
                float accScore = it->first;
                KeyFramePtr pBestKF = pKFi;
                for(vector<KeyFramePtr>::iterator vit=vpNeighs.begin(), vend=vpNeighs.end(); vit!=vend; vit++)
                {
                    KeyFramePtr pKF2 = *vit;
                    if(pKF2->mnMergeQuery==pKF->mnId && pKF2->mnMergeWords>minCommonWords)
                    {
                        accScore+=pKF2->mMergeScore;
                        if(pKF2->mMergeScore>bestScore)
                        {
                            pBestKF=pKF2;
                            bestScore = pKF2->mMergeScore;
                        }
                    }
                }

                lAccScoreAndMatch.push_back(make_pair(accScore,pBestKF));
                if(accScore>bestAccScore)
                    bestAccScore=accScore;
            }

            // Return all those keyframes with a score higher than 0.75*bestScore
            float minScoreToRetain = 0.75f*bestAccScore;

            //cout << "Min score to retain: " << minScoreToRetain << endl;

            set<KeyFramePtr> spAlreadyAddedKF;
            vpMergeCand.reserve(lAccScoreAndMatch.size());

            for(list<pair<float,KeyFramePtr> >::iterator it=lAccScoreAndMatch.begin(), itend=lAccScoreAndMatch.end(); it!=itend; it++)
            {
                if(it->first>minScoreToRetain)
                {
                    KeyFramePtr pKFi = it->second;
                    if(!spAlreadyAddedKF.count(pKFi))
                    {
                        vpMergeCand.push_back(pKFi);
                        spAlreadyAddedKF.insert(pKFi);
                    }
                }
            }
        }

    }

    for(DBoW2::BowVector::const_iterator vit=pKF->mBowVec.begin(), vend=pKF->mBowVec.end(); vit != vend; vit++)
    {
        list<KeyFramePtr> &lKFs = mvInvertedFile[vit->first];

        for(list<KeyFramePtr>::iterator lit=lKFs.begin(), lend= lKFs.end(); lit!=lend; lit++)
        {
            KeyFramePtr pKFi=*lit;
            pKFi->mnLoopQuery=-1;
            pKFi->mnMergeQuery=-1;
        }
    }

}

void KeyFrameDatabase::DetectBestCandidates(KeyFramePtr pKF, vector<KeyFramePtr> &vpLoopCand, vector<KeyFramePtr> &vpMergeCand, int nMinWords)
{
    list<KeyFramePtr> lKFsSharingWords;
    set<KeyFramePtr> spConnectedKF;

    // Search all keyframes that share a word with current frame
    {
        unique_lock<mutex> lock(mMutex);

        spConnectedKF = pKF->GetConnectedKeyFrames();

        for(DBoW2::BowVector::const_iterator vit=pKF->mBowVec.begin(), vend=pKF->mBowVec.end(); vit != vend; vit++)
        {
            list<KeyFramePtr> &lKFs =   mvInvertedFile[vit->first];

            for(list<KeyFramePtr>::iterator lit=lKFs.begin(), lend= lKFs.end(); lit!=lend; lit++)
            {
                KeyFramePtr pKFi=*lit;
                if(spConnectedKF.find(pKFi) != spConnectedKF.end())
                {
                    continue;
                }
                if(pKFi->mnPlaceRecognitionQuery!=pKF->mnId)
                {
                    pKFi->mnPlaceRecognitionWords=0;
                    pKFi->mnPlaceRecognitionQuery=pKF->mnId;
                    lKFsSharingWords.push_back(pKFi);
                }
               pKFi->mnPlaceRecognitionWords++;

            }
        }
    }
    if(lKFsSharingWords.empty())
        return;

    // Only compare against those keyframes that share enough words
    int maxCommonWords=0;
    for(list<KeyFramePtr>::iterator lit=lKFsSharingWords.begin(), lend= lKFsSharingWords.end(); lit!=lend; lit++)
    {
        if((*lit)->mnPlaceRecognitionWords>maxCommonWords)
            maxCommonWords=(*lit)->mnPlaceRecognitionWords;
    }

    int minCommonWords = maxCommonWords*0.8f;

    if(minCommonWords < nMinWords)
    {
        minCommonWords = nMinWords;
    }

    list<pair<float,KeyFramePtr> > lScoreAndMatch;

    int nscores=0;

    // Compute similarity score.
    for(list<KeyFramePtr>::iterator lit=lKFsSharingWords.begin(), lend= lKFsSharingWords.end(); lit!=lend; lit++)
    {
        KeyFramePtr pKFi = *lit;

        if(pKFi->mnPlaceRecognitionWords>minCommonWords)
        {
            nscores++;
            float si = mpVoc->score(pKF->mBowVec,pKFi->mBowVec);
            pKFi->mPlaceRecognitionScore=si;
            lScoreAndMatch.push_back(make_pair(si,pKFi));
        }
    }

    if(lScoreAndMatch.empty())
        return;

    list<pair<float,KeyFramePtr> > lAccScoreAndMatch;
    float bestAccScore = 0;

    // Lets now accumulate score by covisibility
    for(list<pair<float,KeyFramePtr> >::iterator it=lScoreAndMatch.begin(), itend=lScoreAndMatch.end(); it!=itend; it++)
    {
        KeyFramePtr pKFi = it->second;
        vector<KeyFramePtr> vpNeighs = pKFi->GetBestCovisibilityKeyFrames(10);

        float bestScore = it->first;
        float accScore = bestScore;
        KeyFramePtr pBestKF = pKFi;
        for(vector<KeyFramePtr>::iterator vit=vpNeighs.begin(), vend=vpNeighs.end(); vit!=vend; vit++)
        {
            KeyFramePtr pKF2 = *vit;
            if(pKF2->mnPlaceRecognitionQuery!=pKF->mnId)
                continue;

            accScore+=pKF2->mPlaceRecognitionScore;
            if(pKF2->mPlaceRecognitionScore>bestScore)
            {
                pBestKF=pKF2;
                bestScore = pKF2->mPlaceRecognitionScore;
            }

        }
        lAccScoreAndMatch.push_back(make_pair(accScore,pBestKF));
        if(accScore>bestAccScore)
            bestAccScore=accScore;
    }

    // Return all those keyframes with a score higher than 0.75*bestScore
    float minScoreToRetain = 0.75f*bestAccScore;
    set<KeyFramePtr> spAlreadyAddedKF;
    vpLoopCand.reserve(lAccScoreAndMatch.size());
    vpMergeCand.reserve(lAccScoreAndMatch.size());
    for(list<pair<float,KeyFramePtr> >::iterator it=lAccScoreAndMatch.begin(), itend=lAccScoreAndMatch.end(); it!=itend; it++)
    {
        const float &si = it->first;
        if(si>minScoreToRetain)
        {
            KeyFramePtr pKFi = it->second;
            if(!spAlreadyAddedKF.count(pKFi))
            {
                if(pKF->GetMap() == pKFi->GetMap())
                {
                    vpLoopCand.push_back(pKFi);
                }
                else
                {
                    vpMergeCand.push_back(pKFi);
                }
                spAlreadyAddedKF.insert(pKFi);
            }
        }
    }
}

bool compFirst(const pair<float, KeyFramePtr> & a, const pair<float, KeyFramePtr> & b)
{
    return a.first > b.first;
}


void KeyFrameDatabase::DetectNBestCandidates(KeyFramePtr pKF, vector<KeyFramePtr> &vpLoopCand, vector<KeyFramePtr> &vpMergeCand, int nNumCandidates)
{
    list<KeyFramePtr> lKFsSharingWords;
    set<KeyFramePtr> spConnectedKF;

    // Search all keyframes that share a word with current frame
    {
        unique_lock<mutex> lock(mMutex);

        spConnectedKF = pKF->GetConnectedKeyFrames();

        for(DBoW2::BowVector::const_iterator vit=pKF->mBowVec.begin(), vend=pKF->mBowVec.end(); vit != vend; vit++)
        {
            list<KeyFramePtr> &lKFs =   mvInvertedFile[vit->first];

            for(list<KeyFramePtr>::iterator lit=lKFs.begin(), lend= lKFs.end(); lit!=lend; lit++)
            {
                KeyFramePtr pKFi=*lit;

                if(pKFi->mnPlaceRecognitionQuery!=pKF->mnId)
                {
                    pKFi->mnPlaceRecognitionWords=0;
                    if(!spConnectedKF.count(pKFi))
                    {

                        pKFi->mnPlaceRecognitionQuery=pKF->mnId;
                        lKFsSharingWords.push_back(pKFi);
                    }
                }
                pKFi->mnPlaceRecognitionWords++;
            }
        }
    }
    if(lKFsSharingWords.empty())
        return;

    // Only compare against those keyframes that share enough words
    int maxCommonWords=0;
    for(list<KeyFramePtr>::iterator lit=lKFsSharingWords.begin(), lend= lKFsSharingWords.end(); lit!=lend; lit++)
    {
        if((*lit)->mnPlaceRecognitionWords>maxCommonWords)
            maxCommonWords=(*lit)->mnPlaceRecognitionWords;
    }

    int minCommonWords = maxCommonWords*0.8f;

    list<pair<float,KeyFramePtr> > lScoreAndMatch;

    int nscores=0;

    // Compute similarity score.
    for(list<KeyFramePtr>::iterator lit=lKFsSharingWords.begin(), lend= lKFsSharingWords.end(); lit!=lend; lit++)
    {
        KeyFramePtr pKFi = *lit;

        if(pKFi->mnPlaceRecognitionWords>minCommonWords)
        {
            nscores++;
            float si = mpVoc->score(pKF->mBowVec,pKFi->mBowVec);
            pKFi->mPlaceRecognitionScore=si;
            lScoreAndMatch.push_back(make_pair(si,pKFi));
        }
    }

    if(lScoreAndMatch.empty())
        return;

    list<pair<float,KeyFramePtr> > lAccScoreAndMatch;
    float bestAccScore = 0;

    // Lets now accumulate score by covisibility
    for(list<pair<float,KeyFramePtr> >::iterator it=lScoreAndMatch.begin(), itend=lScoreAndMatch.end(); it!=itend; it++)
    {
        KeyFramePtr pKFi = it->second;
        vector<KeyFramePtr> vpNeighs = pKFi->GetBestCovisibilityKeyFrames(10);

        float bestScore = it->first;
        float accScore = bestScore;
        KeyFramePtr pBestKF = pKFi;
        for(vector<KeyFramePtr>::iterator vit=vpNeighs.begin(), vend=vpNeighs.end(); vit!=vend; vit++)
        {
            KeyFramePtr pKF2 = *vit;
            if(pKF2->mnPlaceRecognitionQuery!=pKF->mnId)
                continue;

            accScore+=pKF2->mPlaceRecognitionScore;
            if(pKF2->mPlaceRecognitionScore>bestScore)
            {
                pBestKF=pKF2;
                bestScore = pKF2->mPlaceRecognitionScore;
            }

        }
        lAccScoreAndMatch.push_back(make_pair(accScore,pBestKF));
        if(accScore>bestAccScore)
            bestAccScore=accScore;
    }

    lAccScoreAndMatch.sort(compFirst);

    vpLoopCand.reserve(nNumCandidates);
    vpMergeCand.reserve(nNumCandidates);
    set<KeyFramePtr> spAlreadyAddedKF;
    int i = 0;
    list<pair<float,KeyFramePtr> >::iterator it=lAccScoreAndMatch.begin();
    while(i < lAccScoreAndMatch.size() && (vpLoopCand.size() < nNumCandidates || vpMergeCand.size() < nNumCandidates))
    {
        KeyFramePtr pKFi = it->second;
        if(pKFi->isBad())
            continue;

        if(!spAlreadyAddedKF.count(pKFi))
        {
            if(pKF->GetMap() == pKFi->GetMap() && vpLoopCand.size() < nNumCandidates)
            {
                vpLoopCand.push_back(pKFi);
            }
            else if(pKF->GetMap() != pKFi->GetMap() && vpMergeCand.size() < nNumCandidates && !pKFi->GetMap()->IsBad())
            {
                vpMergeCand.push_back(pKFi);
            }
            spAlreadyAddedKF.insert(pKFi);
        }
        i++;
        it++;
    }
}


vector<KeyFramePtr> KeyFrameDatabase::DetectRelocalizationCandidates(Frame *F, Map* pMap)
{
    list<KeyFramePtr> lKFsSharingWords;

    // Search all keyframes that share a word with current frame
    {
        unique_lock<mutex> lock(mMutex);

        //std::cout << "frame " << F->mnId << " -> mBowVec: " << F->mBowVec << std::endl; 
        for(DBoW2::BowVector::const_iterator vit=F->mBowVec.begin(), vend=F->mBowVec.end(); vit != vend; vit++)
        {
            list<KeyFramePtr> &lKFs = mvInvertedFile[vit->first];
            // if(lKFs.size() != 0)
            //     std::cout << "wordId: " << vit->first <<" -> lKFs.size(): " << lKFs.size() << std::endl; 
    
            for(list<KeyFramePtr>::iterator lit=lKFs.begin(), lend= lKFs.end(); lit!=lend; lit++)
            {
                KeyFramePtr pKFi=*lit;
                if(pKFi->mnRelocQuery!=F->mnId)
                {
                    pKFi->mnRelocWords=0;
                    pKFi->mnRelocQuery=F->mnId;
                    lKFsSharingWords.push_back(pKFi);
                }
                pKFi->mnRelocWords++;
            }
        }
    }

    //std::cout << "lKFsSharingWords.size(): " << lKFsSharingWords.size() << std::endl;

    if(lKFsSharingWords.empty())
        return vector<KeyFramePtr>();

    // Only compare against those keyframes that share enough words
    int maxCommonWords=0;
    for(list<KeyFramePtr>::iterator lit=lKFsSharingWords.begin(), lend= lKFsSharingWords.end(); lit!=lend; lit++)
    {
        if((*lit)->mnRelocWords>maxCommonWords)
            maxCommonWords=(*lit)->mnRelocWords;
    }

    int minCommonWords = maxCommonWords*0.8f;

    list<pair<float,KeyFramePtr> > lScoreAndMatch;

    int nscores=0;

    // Compute similarity score.
    for(list<KeyFramePtr>::iterator lit=lKFsSharingWords.begin(), lend= lKFsSharingWords.end(); lit!=lend; lit++)
    {
        KeyFramePtr pKFi = *lit;

        if(pKFi->mnRelocWords>minCommonWords)
        {
            nscores++;
            float si = mpVoc->score(F->mBowVec,pKFi->mBowVec);
            pKFi->mRelocScore=si;
            lScoreAndMatch.push_back(make_pair(si,pKFi));
        }
    }

    if(lScoreAndMatch.empty())
        return vector<KeyFramePtr>();

    list<pair<float,KeyFramePtr> > lAccScoreAndMatch;
    float bestAccScore = 0;

    // Lets now accumulate score by covisibility
    for(list<pair<float,KeyFramePtr> >::iterator it=lScoreAndMatch.begin(), itend=lScoreAndMatch.end(); it!=itend; it++)
    {
        KeyFramePtr pKFi = it->second;
        vector<KeyFramePtr> vpNeighs = pKFi->GetBestCovisibilityKeyFrames(10);

        float bestScore = it->first;
        float accScore = bestScore;
        KeyFramePtr pBestKF = pKFi;
        for(vector<KeyFramePtr>::iterator vit=vpNeighs.begin(), vend=vpNeighs.end(); vit!=vend; vit++)
        {
            KeyFramePtr pKF2 = *vit;
            if(pKF2->mnRelocQuery!=F->mnId)
                continue;

            accScore+=pKF2->mRelocScore;
            if(pKF2->mRelocScore>bestScore)
            {
                pBestKF=pKF2;
                bestScore = pKF2->mRelocScore;
            }

        }
        lAccScoreAndMatch.push_back(make_pair(accScore,pBestKF));
        if(accScore>bestAccScore)
            bestAccScore=accScore;
    }

    // Return all those keyframes with a score higher than 0.75*bestScore
    float minScoreToRetain = 0.75f*bestAccScore;
    set<KeyFramePtr> spAlreadyAddedKF;
    vector<KeyFramePtr> vpRelocCandidates;
    vpRelocCandidates.reserve(lAccScoreAndMatch.size());
    for(list<pair<float,KeyFramePtr> >::iterator it=lAccScoreAndMatch.begin(), itend=lAccScoreAndMatch.end(); it!=itend; it++)
    {
        const float &si = it->first;
        if(si>minScoreToRetain)
        {
            KeyFramePtr pKFi = it->second;
            if (pKFi->GetMap() != pMap)
                continue;
            if(!spAlreadyAddedKF.count(pKFi))
            {
                vpRelocCandidates.push_back(pKFi);
                spAlreadyAddedKF.insert(pKFi);
            }
        }
    }

    return vpRelocCandidates;
}

void KeyFrameDatabase::SetORBVocabulary(ORBVocabulary* pORBVoc, bool clearInvertedFile)
{
    ORBVocabulary** ptr;
    ptr = (ORBVocabulary**)( &mpVoc );
    *ptr = pORBVoc;

    if(clearInvertedFile){
        mvInvertedFile.clear();
        mvInvertedFile.resize(mpVoc->size());
    }
#if 0
    for(int ii=0; ii<mvInvertedFile.size(); ii++) {
        if(!mvInvertedFile[ii].empty()) std::cout << "<" << ii << ", " << mvInvertedFile[ii].size() << ">" << std::endl; 
    }
#endif 
}

template<class Archive>
void KeyFrameDatabase::serialize(Archive& ar, const unsigned int version)
{
    UNUSED_VAR(version);

    // don't save associated vocabulary, KFDB restore by created explicitly from a new ORBvocabulary instance
    // inverted file
    ar & mvInvertedFile;
}
template void KeyFrameDatabase::serialize(boost::archive::binary_iarchive&, const unsigned int);
template void KeyFrameDatabase::serialize(boost::archive::binary_oarchive&, const unsigned int);
template void KeyFrameDatabase::serialize(boost::archive::text_iarchive&, const unsigned int);
template void KeyFrameDatabase::serialize(boost::archive::text_oarchive&, const unsigned int);

} // namespace PLVS2
